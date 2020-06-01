package com.github.stsaz.fmedia;

import android.app.Activity;
import android.content.ComponentName;
import android.support.v4.media.MediaBrowserCompat;
import android.support.v4.media.MediaMetadataCompat;
import android.support.v4.media.session.MediaControllerCompat;
import android.support.v4.media.session.PlaybackStateCompat;

/**
 * Bridge between UI and audio service.
 */
class TrackCtl {
	private final String TAG = "TrackCtl";
	private Core core;
	private Activity activity;

	private MediaBrowserCompat browser;
	private MediaControllerCompat ctl;

	TrackCtl(Core cor, Activity activity) {
		core = cor;
		this.activity = activity;
		browser = new MediaBrowserCompat(core.context, new ComponentName(core.context, Svc.class),
				new MediaBrowserCompat.ConnectionCallback() {
					@Override
					public void onConnected() {
						core.dbglog(TAG, "MediaBrowserCompat.onConnected");
						on_connected();
					}
				},
				null
		);
	}

	void close() {
		core.dbglog(TAG, "browser.disconnect");
		browser.disconnect();
	}

	void connect() {
		browser.connect();
	}

	/**
	 * Called by MediaBrowser when it's connected to the audio service
	 */
	private void on_connected() {
		try {
			ctl = new MediaControllerCompat(activity, browser.getSessionToken());
		} catch (Exception e) {
			core.errlog(TAG, "%s", e);
			return;
		}
		ctl.registerCallback(new MediaControllerCompat.Callback() {
			@Override
			public void onSessionReady() {
				core.dbglog(TAG, "MediaControllerCompat.onSessionReady");
			}

			@Override
			public void onSessionDestroyed() {
				core.dbglog(TAG, "MediaControllerCompat.onSessionDestroyed");
			}

			@Override
			public void onPlaybackStateChanged(PlaybackStateCompat state) {
				// core.dbglog(TAG, "MediaControllerCompat.onPlaybackStateChanged: %d", state.getState());
			}

			@Override
			public void onMetadataChanged(MediaMetadataCompat metadata) {
				core.dbglog(TAG, "MediaControllerCompat.onMetadataChanged");
			}
		});
		MediaControllerCompat.setMediaController(activity, ctl);
	}

	void play(int pos) {
		ctl.getTransportControls().skipToQueueItem(pos);
	}

	void pause() {
		ctl.getTransportControls().pause();
	}

	void unpause() {
		ctl.getTransportControls().play();
	}

	/*void stop() {
		core.dbglog(TAG, "stop");
		ctl.getTransportControls().stop();
	}*/

	void next() {
		ctl.getTransportControls().skipToNext();
	}

	void prev() {
		ctl.getTransportControls().skipToPrevious();
	}

	void seek(int pos) {
		ctl.getTransportControls().seekTo(pos);
	}
}
