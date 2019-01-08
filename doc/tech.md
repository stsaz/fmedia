# Technical details

* Queue
	* Items positioning
	* UI interaction


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

