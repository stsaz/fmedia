# Technical details

* Creating and starting tracks
* Stopping the track
* Seeking
* .ogg write
* Queue
	* Items positioning
	* UI interaction
* Filter: Auto Attenuator

## Creating and starting tracks

```c
	#Main                   #Worker1        #Worker2
	==================================================
	track->create()
	track->cmd(...)
	track->cmd(FMED_TRACK_XSTART):
	  core->cmd(FMED_XASSIGN) -> *
	  core->cmd(FMED_XADD) ->
	                        trk_process()
	--
	track->create()
	track->cmd(...)
	track->cmd(FMED_TRACK_XSTART):
	  core->cmd(FMED_XASSIGN) ------------> *
	  core->cmd(FMED_XADD) ---------------> trk_process()
```

## Stopping the track

Case 1.  Stop-signal is received after the chain has been finished.

```c
	#Worker         #Main
	==============================
	trk_process():
	  FINISHED=1
	                FMED_TRACK_STOP:
	                  (FINISHED):
	                    #Worker <- trk_process
	--
	trk_process():
	  (FINISHED):
	    close()
```

Case 2.  Stop-signal is received while processing the track's filter chain.

```c
	#Worker         #Main
	==============================
	                FMED_TRACK_STOP:
	                  (!FINISHED):
	trk_process():      STOP=1
	  (STOP):
	    FSTOP=1
	  ...
	  (STOP):
        close()
```

Case 3.  Stop-signal is received while the chain is suspended after FMED_RASYNC.

```c
	#Worker         #Main           #Audio
	================================================
	                FMED_TRACK_STOP:
	                  (!FINISHED):
	                    STOP=1
	                                FMED_TRACK_WAKE:
	                                  #Worker <- trk_process
	trk_process():
	  (STOP):
	    FSTOP=1
	  ...
	  (STOP):
	    close()
```

## Seeking

Case 1.  Seek position is initially set (e.g. with --seek=TIME).

```C
	           fmt            dec        ui
	=========================================
	S=N,Q=1 -> (Q&&S!=''):
	           seek(S),Q=0 -> dec()
	           *           <- *
	           (!Q)        -> skip(S) -> S=''
```

`S` - seek position.
`Q` - seek request flag.

Case 2.  Seek position is set in 'ui' module by user's command while the currently active filter is 'ui' itself or any previous filter.
Key factor: 'ui' can't update seek position `S` while it's still in use by previous filters.
Note: 'dec' may check `Q` (which means a new seek request is received) to interrupt the current decoding process.

```C
	file      fmt             dec        ui       aout
	==============================================================
	                                   s=N,Q=1 -> stop()
	... <-> ...         <-> ...
	                        (Q)     -> *
	        (Q&&S!=''): <------------- S=s
	        seek(S),Q=0 --> dec()
	        *           <-- *
	        (!Q)        --> skip(S) -> S=''    -> clear(),write()
```

Case 3.  Seek position is set in 'ui' module by user's command while the currently active filter is 'aout' or previous filters.
Key factor: 'aout' discards all current data (due to `Q`) and requests new data immediately.

```C
	fmt            dec        ui         aout               core
	============================================================
	                          s=N,Q=1 -> stop()          -> *
	(Q&&S!=''): <------------ S=s     <- (Q)             <- *
	seek(S),Q=0 -> dec()
	               skip(S) -> S=''    -> clear(),write()
```

Case 4.  Seeking on .cue tracks.  These tracks have `abs_seek` value set.
Key factor: 'fmt' and 'dec' know nothing about `abs_seek`;
 'cuehook' module updates current position and seek position values.

```C
	track    fmt              dec        cuehook    ui
	====================================================
	S+=AS -> seek(S),POS=S -> dec()
	                          skip(S) -> POS-=AS -> S=''
	         *             <------------ S+=AS   <- S=N
	         ...
```

`POS` - current audio position.
`AS` - audio offset for the current .cue track (`abs_seek`).


## .ogg write

Case 1.  Add packets normally with known audio position of every packet.
Note: `GEN_OPUS_TAG` flag is a questionable solution for mkv->ogg copying.

```C
	mkv.in              ogg.out                      file.out
	==============================================================
	GEN_OPUS_TAG=1
	POS='',PKT=.     -> cache=PKT //e.g. Opus hdr
	POS=n,PKT=.      -> write(cache,end=0,flush)  -> *
	                    GEN_OPUS_TAG:
	                    write("...",end=0,flush)  -> *
	*                <- cache=PKT                 <- *
	POS=n+1,PKT=.    -> write(cache,end=POS)
	*                <- cache=PKT
	           ...
	POS=.,PKT=.      -> write(cache,end=POS)      -> *
	*                <- cache=PKT                 <- *
	           ...
	POS=.,PKT=.,DONE -> write(cache,end=POS)
	                    write(PKT,end=TOTAL,last) -> *
```

Case 2.  Input is OGG with only end-position value of each page.

```C
	ogg.in         ogg.out        file.out
	=================================================================
	END=0,FLUSH -> write()     -> * //e.g. Opus hdr
	END=0,FLUSH -> write()     -> * //e.g. Opus tags
	END=-1      -> write()
	       ...
	END=.,FLUSH -> write()     -> *
	DONE        -> write(last) -> *
```

## Queue

### Items positioning

This section describes how the playlist entries and their data are organized internally.

Items are stored within a list:

	entry0 <-> entry1 <-> ... <-> entryN

Each item is allocated separately on the heap.  This way it's guaranteed that an item has its unique ID which is necessary to associate an item with the active track, and this ID won't be changed while the track is running.

Items' position numbering within the list is available via a solid buffer of 4/8-bytes elements - pointers to real objects:

	[0]: entry0*
	[1]: entry1*
	...
	[N]: entryN*

Each item also stores its position internally when it's first added to list.  This is necessary for a fast `FMED_QUE_ID` handler.  Uncached `FMED_QUE_ID` is slow: the larger the item number - the slower the process gets.  `FMED_QUE_ID` updates position whenever it sees the change.  As a result, `FMED_QUE_ID` is slow only for those items whose position were changed.

	entry0 {
		position = 0
	}

When a new item is added/deleted to/from the middle, all next elements are shifted to maintain the order.  The process is not optimal, but rare.  Note that we can't possibly update all positions stored inside each item, so for all next items their internally stored positions become invalid.

	[0]: entry0* -> {position = 0}
	[1]: entry1* -> {position = 1}  // to remove
	[2]: entry2* -> {position = 2}
	...
	[N]: entryN* -> {position = N}

	->

	[0]: entry0* -> {position = 0}
	[1]: entry2* -> {position = 2 (invalid)}
	...
	[N]: entryN* -> {position = N (invalid)}


### UI interaction

UI needs the information about a playlist entry in a synchronous manner (from UI thread).
Since UI doesn't modify data in this case, we need to serialize main thread's write access and UI thread's read access to an entry's information.

UI gets item ID by its index and locks the item until it has read the information it needs.

* main thread can't add or remove playlists without UI thread's notice - we don't need to protect playlist pointers

* main thread may clear the current playlist, add/remove item - these events are handled in onchange() callback, which synchronously notifies UI thread, but after the change is done.  We need to use per-playlist lock *before* the change is made.

* main thread may change item's meta info when the item is active.  This means that meta_set() must use per-item lock.


## Filter: Auto Attenuator

Automatically reduce the volume of loud tracks so there will be less volume difference between the loud and quiet tracks.

Config:

	ceiling_db = -6dB

Chain:

	1.1 ... -> Converter --(int16)-> AA --(float)-> AudioOutput
	1.2 ... -> Converter <-(convert:float/i)-- AA
	2.  ... -> Converter --(float)-> AA --(float)-> ...

Algorithm:

	track_gain := 1.0
	track_ceiling := conf.ceiling
	...
	if sample > track_ceiling
		track_gain = 1.0 - (sample - conf.ceiling)
		track_ceiling = sample
	sample = sample * track_gain
