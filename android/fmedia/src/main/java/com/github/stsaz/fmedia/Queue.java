package com.github.stsaz.fmedia;

import android.annotation.SuppressLint;
import android.os.Handler;
import android.os.Looper;

import java.io.BufferedOutputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.Random;

interface QueueNotify {
	/**
	 * Called when the queue is modififed.
	 */
	void on_change(int what);
}

class PList {
	ArrayList<String> plist;
	int curpos = -1;
	boolean modified;
}

class Queue {
	private final String TAG = "Queue";
	private Core core;
	private Track track;
	private ArrayList<QueueNotify> nfy;

	private PList pl;
	private boolean repeat;
	private boolean random;
	private boolean active;
	private Random rnd;
	private Handler mloop;

	Queue(Core core) {
		this.core = core;
		track = core.track();
		track.filter_add(new Filter() {
			@Override
			public int open(TrackHandle t) {
				active = true;
				return 0;
			}

			@Override
			public void close(TrackHandle t) {
				on_close(t);
			}
		});
		pl = new PList();
		pl.plist = new ArrayList<>();
		nfy = new ArrayList<>();

		mloop = new Handler(Looper.getMainLooper());

		load(core.work_dir + "/list1.m3u8");
	}

	void close() {
	}

	void saveconf() {
		if (pl.modified)
			save(core.work_dir + "/list1.m3u8");
	}

	void nfy_add(QueueNotify qn) {
		nfy.add(qn);
	}

	void nfy_rm(QueueNotify qn) {
		nfy.remove(qn);
	}

	private void nfy_all() {
		for (QueueNotify qn : nfy) {
			qn.on_change(0);
		}
	}

	/**
	 * Play track at cursor
	 */
	void playcur() {
		int pos = pl.curpos;
		if (pos < 0)
			pos = 0;
		play(pos);
	}

	/**
	 * Play track at the specified position
	 */
	void play(int index) {
		core.dbglog(TAG, "play: %d", index);
		if (active)
			track.stop();

		if (index < 0 || index >= pl.plist.size())
			return;

		pl.curpos = index;
		track.start(pl.plist.get(index));
	}

	/**
	 * Play next track
	 */
	void next() {
		int i = pl.curpos + 1;
		if (random) {
			if (pl.plist.size() == 0)
				return;
			i = rnd.nextInt(pl.plist.size());
			if (i == pl.curpos) {
				i = pl.curpos + 1;
				if (i == pl.plist.size())
					i = 0;
			}
		} else if (repeat) {
			if (i == pl.plist.size())
				i = 0;
		}
		play(i);
	}

	/**
	 * Play previous track
	 */
	void prev() {
		play(pl.curpos - 1);
	}

	/**
	 * Called after a track has been finished.
	 */
	private void on_close(TrackHandle t) {
		active = false;
		if (!t.stopped && !t.error) {
			mloop.post(() -> next());
		}
	}

	/**
	 * Set Random switch
	 */
	void random(boolean val) {
		random = val;
		if (val)
			rnd = new Random(new Date().getTime());
	}

	boolean is_random() {
		return random;
	}

	void repeat(boolean val) {
		repeat = val;
	}

	boolean is_repeat() {
		return repeat;
	}

	String[] list() {
		return pl.plist.toArray(new String[0]);
	}

	void remove(int pos) {
		core.dbglog(TAG, "remove: %d", pos);
		pl.plist.remove(pos);
		if (pos <= pl.curpos)
			pl.curpos--;
	}

	/**
	 * Clear playlist
	 */
	void clear() {
		core.dbglog(TAG, "clear");
		pl.plist.clear();
		pl.curpos = -1;
		pl.modified = true;
		nfy_all();
	}

	void clear_addmany(String[] urls) {
		pl.plist.clear();
		pl.curpos = -1;
		addmany(urls);
	}

	void addmany(String[] urls) {
		pl.plist.addAll(Arrays.asList(urls));
		pl.modified = true;
		nfy_all();
	}

	/**
	 * Add an entry
	 */
	void add(String url) {
		core.dbglog(TAG, "add: %s", url);
		pl.plist.add(url);
		pl.modified = true;
		nfy_all();
	}

	/**
	 * Save playlist to a file on disk
	 */
	private void save(String fn) {
		StringBuilder sb = new StringBuilder();
		for (String s : pl.plist) {
			sb.append(s);
			sb.append('\n');
		}
		if (core.file_writeall(fn, sb.toString().getBytes(), Core.FILE_WRITE_SAFE))
			core.dbglog(TAG, "saved %d items to %s", pl.plist.size(), fn);
	}

	void save_stream(OutputStream os) {
		StringBuilder sb = new StringBuilder();
		for (String s : pl.plist) {
			sb.append(s);
			sb.append('\n');
		}
		try {
			BufferedOutputStream bo = new BufferedOutputStream(os);
			bo.write(sb.toString().getBytes());
			bo.close();
			os.close();
			core.dbglog(TAG, "saved %d items to a file", pl.plist.size());
		} catch (Exception e) {
			core.errlog(TAG, "save_stream: %s", e);
		}
	}

	/**
	 * Load playlist from a file on disk
	 */
	private void load(String fn) {
		byte[] b = core.file_readall(fn);
		if (b == null)
			return;

		load_data(b);
		core.dbglog(TAG, "loaded %d items from %s", pl.plist.size(), fn);
	}

	void load_data(byte[] data) {
		String bs = new String(data);
		pl.plist.clear();
		Splitter spl = new Splitter();
		while (true) {
			String s = spl.next(bs, '\n');
			if (s == null)
				break;
			if (s.length() != 0)
				pl.plist.add(s);
		}
		core.dbglog(TAG, "loaded %d items", pl.plist.size());
	}

	/*
	curpos 0..N
	random 0|1
	repeat 0|1
	*/
	@SuppressLint("DefaultLocale")
	String writeconf() {
		StringBuilder s = new StringBuilder();
		if (pl.curpos >= 0)
			s.append(String.format("curpos %d\n", pl.curpos));

		s.append(String.format("random %d\n", core.bool_to_int(random)));
		s.append(String.format("repeat %d\n", core.bool_to_int(repeat)));

		return s.toString();
	}

	int readconf(String k, String v) {
		if (k.equals("curpos")) {
			pl.curpos = core.str_to_int(v, 0);
			pl.curpos = Math.min(pl.curpos, pl.plist.size());
			return 0;

		} else if (k.equals("random")) {
			int val = core.str_to_int(v, 0);
			if (val == 1)
				random(true);
			return 0;
		}
		return 1;
	}

	/**
	 * Get currently playing track index
	 */
	int cur() {
		if (!active)
			return -1;
		return pl.curpos;
	}
}
