package com.github.stsaz.fmedia;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.support.v4.media.MediaBrowserCompat;
import android.support.v4.media.MediaDescriptionCompat;
import android.support.v4.media.MediaMetadataCompat;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;

import androidx.annotation.NonNull;
import androidx.core.app.NotificationCompat;
import androidx.media.MediaBrowserServiceCompat;
import androidx.media.app.NotificationCompat.MediaStyle;
import androidx.media.session.MediaButtonReceiver;

import java.util.ArrayList;
import java.util.List;

public class Svc extends MediaBrowserServiceCompat {
	private final String TAG = "Svc";
	Core core;
	Track track;
	Queue queue;
	int playtime; // current track's progress

	MediaSessionCompat sess;
	int state;
	PlaybackStateCompat.Builder pstate;
	NotificationCompat.Builder nfy;
	String nfy_chan;
	Handler mloop;
	Runnable delayed_stop;

	@Override
	public void onCreate() {
		super.onCreate();

		core = Core.getInstance();
		queue = core.queue();
		queue.nfy_add(new QueueNotify() {
			@Override
			public void on_change(int what) {
				sess_setqueue();
			}
		});
		track = core.track();
		track.filter_add(new Filter() {
			@Override
			public int open(TrackHandle t) {
				return new_track(t);
			}

			@Override
			public void close(TrackHandle t) {
				close_track(t);
			}

			@Override
			public int process(TrackHandle t) {
				return update_track(playtime);
			}
		});

		mloop = new Handler(Looper.getMainLooper());
		delayed_stop = new Runnable() {
			public void run() {
				stop_delayed();
			}
		};

		sess_init();
		sess_setqueue();
		setSessionToken(sess.getSessionToken());
		fg_prep();
		core.dbglog(TAG, "init");
	}

	@Override
	public void onDestroy() {
		core.dbglog(TAG, "onDestroy");
		sess.release();
		core.close();
		super.onDestroy();
	}

	@Override
	public int onStartCommand(Intent intent, int flags, int startId) {
		core.dbglog(TAG, "onStartCommand");
		MediaButtonReceiver.handleIntent(sess, intent);
		return super.onStartCommand(intent, flags, startId);
	}

	@Override
	public BrowserRoot onGetRoot(@NonNull String clientPackageName, int clientUid,
								 Bundle rootHints) {
		return new BrowserRoot(getString(R.string.app_name), null);
	}

	@Override
	public void onLoadChildren(@NonNull final String parentMediaId,
							   @NonNull final Result<List<MediaBrowserCompat.MediaItem>> result) {
	}

	/**
	 * Initialize session
	 */
	void sess_init() {
		ComponentName mbr = new ComponentName(getApplicationContext(), MediaButtonReceiver.class);
		sess = new MediaSessionCompat(getApplicationContext(), TAG, mbr, null);

		pstate = new PlaybackStateCompat.Builder();
		pstate.setActions(PlaybackStateCompat.ACTION_PLAY
				| PlaybackStateCompat.ACTION_STOP
				| PlaybackStateCompat.ACTION_PAUSE
				| PlaybackStateCompat.ACTION_PLAY_PAUSE
				| PlaybackStateCompat.ACTION_SEEK_TO
				| PlaybackStateCompat.ACTION_SKIP_TO_NEXT
				| PlaybackStateCompat.ACTION_SKIP_TO_PREVIOUS
				| PlaybackStateCompat.ACTION_SKIP_TO_QUEUE_ITEM
				| PlaybackStateCompat.ACTION_SET_SHUFFLE_MODE
				| PlaybackStateCompat.ACTION_SET_REPEAT_MODE);
		pstate.setActiveQueueItemId(0);
		state = PlaybackStateCompat.STATE_STOPPED;
		pstate.setState(state, 0, 0);
		sess.setPlaybackState(pstate.build());

		int mode = PlaybackStateCompat.SHUFFLE_MODE_NONE;
		if (queue.is_random())
			mode = PlaybackStateCompat.SHUFFLE_MODE_ALL;
		sess.setShuffleMode(mode);

		sess.setCallback(new MediaSessionCompat.Callback() {
			@Override
			public void onPlay() {
				core.dbglog(TAG, "MediaSessionCompat.onPlay");
				if (track.state() == Track.State.NONE) {
					queue.playcur();
					return;
				}
				track.unpause();
				fg(null);
			}

			@Override
			public void onPause() {
				core.dbglog(TAG, "MediaSessionCompat.onPause");
				track.pause();
				sess_state(PlaybackStateCompat.STATE_PAUSED, playtime);
				stopForeground(false);
			}

			@Override
			public void onStop() {
				core.dbglog(TAG, "MediaSessionCompat.onStop");
				track.stop();
			}

			@Override
			public void onSkipToQueueItem(long id) {
				core.dbglog(TAG, "MediaSessionCompat.onSkipToQueueItem");
				queue.play((int) id);
			}

			@Override
			public void onSkipToNext() {
				core.dbglog(TAG, "MediaSessionCompat.onSkipToNext");
				queue.next();
			}

			@Override
			public void onSkipToPrevious() {
				core.dbglog(TAG, "MediaSessionCompat.onSkipToPrevious");
				queue.prev();
			}

			@Override
			public void onSeekTo(long pos) {
				core.dbglog(TAG, "MediaSessionCompat.onSeekTo");
				track.seek((int) pos);
			}

			@Override
			public boolean onMediaButtonEvent(Intent mediaButtonEvent) {
				core.dbglog(TAG, "MediaSessionCompat.onMediaButtonEvent");
				return super.onMediaButtonEvent(mediaButtonEvent);
			}

			@Override
			public void onSetShuffleMode(int shuffleMode) {
				core.dbglog(TAG, "MediaSessionCompat.onSetShuffleMode");
				sess.setShuffleMode(shuffleMode);
				queue.random(shuffleMode == PlaybackStateCompat.SHUFFLE_MODE_ALL);
			}

			@Override
			public void onSetRepeatMode(int repeatMode) {
				core.dbglog(TAG, "MediaSessionCompat.onSetRepeatMode");
				boolean val = false;
				if (repeatMode == PlaybackStateCompat.REPEAT_MODE_ALL)
					val = true;
				queue.repeat = val;
				sess.setRepeatMode(repeatMode);
			}
		});
	}

	/**
	 * Set session state
	 */
	void sess_state(int st, int playtime) {
		state = st;
		pstate.setState(state, playtime, 0);
		sess.setPlaybackState(pstate.build());
	}

	/**
	 * Set session queue
	 */
	void sess_setqueue() {
		String[] l = queue.list();
		ArrayList<MediaSessionCompat.QueueItem> q = new ArrayList<>(l.length);
		for (String s : l) {
			MediaDescriptionCompat.Builder b = new MediaDescriptionCompat.Builder();
			b.setMediaId(s);
			MediaSessionCompat.QueueItem i = new MediaSessionCompat.QueueItem(b.build(), 0);
			q.add(i);
		}
		sess.setQueue(q);
	}

	/**
	 * Prepare notification
	 */
	void fg_prep() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			nfy_chan = "fmedia.svc.chan";
			NotificationManager mgr = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
			if (mgr != null) {
				NotificationChannel chan = new NotificationChannel(nfy_chan, "channame", NotificationManager.IMPORTANCE_LOW);
				mgr.createNotificationChannel(chan);
			}
		}

		nfy = new NotificationCompat.Builder(this, nfy_chan);
		nfy.setSmallIcon(R.drawable.ic_launcher_foreground);

		// Launch UI by clicking the notification
		Intent intent = new Intent(this, MainActivity.class);
		PendingIntent contentIntent = PendingIntent.getActivity(this, 0, intent, PendingIntent.FLAG_UPDATE_CURRENT);
		nfy.setContentIntent(contentIntent);

		// Add control buttons
		nfy.addAction(new NotificationCompat.Action(R.drawable.ic_pause, "Pause",
				MediaButtonReceiver.buildMediaButtonPendingIntent(this, PlaybackStateCompat.ACTION_PLAY_PAUSE)));
		nfy.addAction(new NotificationCompat.Action(R.drawable.ic_next, "Next",
				MediaButtonReceiver.buildMediaButtonPendingIntent(this, PlaybackStateCompat.ACTION_SKIP_TO_NEXT)));

		MediaStyle mstyle = new MediaStyle();

		// Show button images
		mstyle.setShowActionsInCompactView(0, 1);
		mstyle.setMediaSession(sess.getSessionToken());

		// Make pause button visible on the lockscreen
		nfy.setVisibility(NotificationCompat.VISIBILITY_PUBLIC);

		// Stop the service when the notification is swiped away (from Android 5)
		nfy.setDeleteIntent(MediaButtonReceiver.buildMediaButtonPendingIntent(this, PlaybackStateCompat.ACTION_STOP));

		// Show Cancel button (before Android 5)
		mstyle.setShowCancelButton(true);
		mstyle.setCancelButtonIntent(MediaButtonReceiver.buildMediaButtonPendingIntent(this, PlaybackStateCompat.ACTION_STOP));

		nfy.setStyle(mstyle);
	}

	/**
	 * Set service as foreground and show notification.
	 */
	void fg(String name) {
		if (name != null) {
			nfy.setContentTitle("Playing");
			nfy.setContentText(name);
		}
		startForeground(1, nfy.build());
		core.dbglog(TAG, "startForeground");
	}

	/**
	 * Called by Track when a new track is initialized
	 */
	int new_track(TrackHandle t) {
		mloop.removeCallbacks(delayed_stop);
		playtime = 0;
		if (state == PlaybackStateCompat.STATE_STOPPED) {
			sess.setActive(true);
			startService(new Intent(this, Svc.class));
		}

		pstate.setActiveQueueItemId(queue.cur());

		MediaMetadataCompat.Builder meta = new MediaMetadataCompat.Builder();
		meta.putString(MediaMetadataCompat.METADATA_KEY_MEDIA_URI, t.name);
		meta.putLong(MediaMetadataCompat.METADATA_KEY_DURATION, t.time_total);
		sess.setMetadata(meta.build());

		sess_state(PlaybackStateCompat.STATE_BUFFERING, 0);
		fg(t.name);
		return 0;
	}

	/**
	 * Called by Track after a track is finished
	 */
	void close_track(TrackHandle t) {
		sess_state(PlaybackStateCompat.STATE_SKIPPING_TO_NEXT, 0);
		mloop.postDelayed(delayed_stop, 1000);
	}

	/**
	 * Called by Track during playback
	 */
	int update_track(int playtime) {
		int t = Svc.this.playtime;
		Svc.this.playtime = playtime;
		if (t == 0 || t / 1000 != playtime / 1000) {
			sess_state(PlaybackStateCompat.STATE_PLAYING, playtime);
		}
		return 0;
	}

	/**
	 * Stop the service after playback is finished
	 */
	void stop_delayed() {
		if (state == PlaybackStateCompat.STATE_PLAYING
				|| state == PlaybackStateCompat.STATE_BUFFERING)
			return;
		sess_state(PlaybackStateCompat.STATE_STOPPED, 0);
		sess.setActive(false);
		stopForeground(true);
		core.dbglog(TAG, "stopForeground, stopSelf");
		stopSelf();
	}
}
