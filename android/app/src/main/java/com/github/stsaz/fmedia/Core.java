package com.github.stsaz.fmedia;

import androidx.annotation.NonNull;

import android.content.Context;
import android.util.Log;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;

class Core extends CoreBase {
	private static Core instance;
	private int refcount;

	private final String TAG = "Core";
	private final String CONF_FN = "fmedia-user.conf";

	private GUI gui;
	private Queue qu;
	private Track track;
	private SysJobs sysjobs;
	private MP mp;

	String work_dir;
	Context context;

	static Core getInstance() {
		instance.dbglog(instance.TAG, "getInstance");
		instance.refcount++;
		return instance;
	}

	static Core init_once(@NonNull Context ctx) {
		if (instance == null) {
			Core c = new Core();
			c.refcount = 1;
			if (0 != c.init(ctx))
				return null;
			instance = c;
			return c;
		}
		return getInstance();
	}

	private int init(@NonNull Context ctx) {
		context = ctx;
		work_dir = ctx.getCacheDir().getPath();

		gui = new GUI(this);
		track = new Track(this);
		qu = new Queue(this);
		mp = new MP();
		mp.init(this);
		sysjobs = new SysJobs();
		sysjobs.init(this);

		loadconf();

		dbglog(TAG, "init");
		return 0;
	}

	void unref() {
		dbglog(TAG, "unref(): %d", refcount);
		refcount--;
	}

	void close() {
		dbglog(TAG, "close(): %d", refcount);
		if (--refcount != 0)
			return;
		instance = null;
		qu.close();
		sysjobs.uninit();
	}

	Queue queue() {
		return qu;
	}

	Track track() {
		return track;
	}

	GUI gui() {
		return gui;
	}

	/**
	 * Save configuration
	 */
	void saveconf() {
		String fn = work_dir + "/" + CONF_FN;
		StringBuilder sb = new StringBuilder();
		sb.append(qu.writeconf());
		sb.append(gui.writeconf());
		if (!file_writeall(fn, sb.toString().getBytes(), FILE_WRITE_SAFE))
			errlog(TAG, "saveconf: %s", fn);
		else
			dbglog(TAG, "saveconf ok: %s", fn);
	}

	/**
	 * Load configuration
	 */
	private void loadconf() {
		String fn = work_dir + "/" + CONF_FN;
		byte[] b = file_readall(fn);
		if (b == null)
			return;
		String bs = new String(b);

		Splitter spl = new Splitter();
		while (true) {
			String ln = spl.next(bs, '\n', 0);
			if (ln == null)
				break;

			Splitter spl2 = new Splitter();
			String k, v;
			k = spl2.next(ln, ' ', 0);
			v = spl2.remainder(ln);
			if (k == null || v == null)
				continue;

			if (0 == qu.readconf(k, v))
				continue;
			gui.readconf(k, v);
		}

		dbglog(TAG, "loadconf: %s: %s", fn, bs);
	}

	void errlog(String mod, String fmt, Object... args) {
		if (BuildConfig.DEBUG)
			Log.e(mod, String.format(fmt, args));
		if (gui != null)
			gui.on_error(fmt, args);
	}

	void dbglog(String mod, String fmt, Object... args) {
		if (BuildConfig.DEBUG)
			Log.d(mod, String.format(fmt, args));
	}
}

abstract class CoreBase {
	private final String TAG = "Core";

	abstract void errlog(String mod, String fmt, Object... args);

	int str_toint(String s, int def) {
		try {
			return Integer.decode(s);
		} catch (Exception e) {
			return def;
		}
	}

	boolean str_tobool(String s) {
		return s.equalsIgnoreCase("true");
	}

	String str_frombool(boolean b) {
		if (b)
			return "true";
		return "false";
	}

	static final int FILE_WRITE_SAFE = 1;

	boolean file_writeall(String fn, byte[] data, int flags) {
		String name = fn;
		if (flags == FILE_WRITE_SAFE)
			name = fn + ".tmp";
		try {
			File f = new File(name);
			FileOutputStream os = new FileOutputStream(f);
			BufferedOutputStream bo = new BufferedOutputStream(os);
			bo.write(data);
			bo.close();
			os.close();
			if (flags == FILE_WRITE_SAFE) {
				if (!f.renameTo(new File(fn))) {
					errlog(TAG, "renameTo() failed");
					return false;
				}
			}
		} catch (Exception e) {
			errlog(TAG, "file_writeall: %s", e);
			return false;
		}

		return true;
	}

	byte[] file_readall(String fn) {
		byte[] b;
		try {
			File f = new File(fn);
			FileInputStream is = new FileInputStream(f);
			int n = (int) f.length();
			b = new byte[n];
			is.read(b, 0, n);
		} catch (Exception e) {
			errlog(TAG, "file_readall: %s", e);
			return null;
		}
		return b;
	}

	boolean file_rename(String old, String newname) {
		boolean r;
		try {
			File f = new File(old);
			r = f.renameTo(new File(newname));
		} catch (Exception e) {
			errlog(TAG, "file_rename: %s", e);
			return false;
		}
		return r;
	}
}

class Splitter {
	private int off;

	/**
	 * Return null if no more entries.
	 */
	String next(String s, char by, int flags) {
		if (off == s.length())
			return null;
		int pos = s.indexOf(by, off);
		String r;
		if (pos == -1) {
			r = s.substring(off);
			off = s.length();
		} else {
			r = s.substring(off, pos);
			off = pos + 1;
		}
		return r;
	}

	String remainder(String s) {
		return s.substring(off);
	}

	static String[] path_split2(String s) {
		int pos = s.lastIndexOf('/');
		String[] parts = new String[2];
		if (pos != -1) {
			parts[0] = s.substring(0, pos);
			parts[1] = s.substring(pos + 1);
		} else {
			parts[0] = "";
			parts[1] = s;
		}
		return parts;
	}
}
