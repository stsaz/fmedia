package com.github.stsaz.fmedia;

import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.Environment;
import android.util.Log;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.util.ArrayList;

class CoreSettings {
	private Core core;
	boolean svc_notification_disable;
	String trash_dir;
	boolean file_del;

	CoreSettings(Core core) {
		this.core = core;
		trash_dir = String.format("%s/Trash", core.storage_path);
	}

	@SuppressLint("DefaultLocale")
	String writeconf() {
		return String.format("svc_notification_disable %d\n", core.bool_to_int(svc_notification_disable)) +
				String.format("file_delete %d\n", core.bool_to_int(file_del)) +
				String.format("trash_dir %s\n", trash_dir);
	}

	int readconf(String k, String v) {
		if (k.equals("svc_notification_disable"))
			svc_notification_disable = core.str_to_bool(v);
		else if (k.equals("file_delete"))
			file_del = core.str_to_bool(v);
		else if (k.equals("trash_dir"))
			trash_dir = v;
		else
			return 1;
		return 0;
	}
}

class Core extends CoreBase {
	private static Core instance;
	private int refcount;

	private static final String TAG = "Core";
	private final String CONF_FN = "fmedia-user.conf";

	private GUI gui;
	private Queue qu;
	private Track track;
	private SysJobs sysjobs;
	private MP mp;

	String storage_path;
	String[] storage_paths;
	String work_dir;
	Context context;
	CoreSettings setts;

	static Core getInstance() {
		instance.dbglog(TAG, "getInstance");
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
		dbglog(TAG, "init");
		context = ctx;
		work_dir = ctx.getFilesDir().getPath();
		storage_path = Environment.getExternalStorageDirectory().getPath();
		storage_paths = system_storage_dirs(ctx);

		setts = new CoreSettings(this);
		gui = new GUI(this);
		track = new Track(this);
		qu = new Queue(this);
		mp = new MP();
		mp.init(this);
		sysjobs = new SysJobs();
		sysjobs.init(this);

		loadconf();
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
		sb.append(this.setts.writeconf());
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
			String ln = spl.next(bs, '\n');
			if (ln == null)
				break;

			Splitter spl2 = new Splitter();
			String k, v;
			k = spl2.next(ln, ' ');
			v = spl2.remainder(ln);
			if (k == null || v == null)
				continue;

			if (0 == setts.readconf(k, v))
				continue;
			if (0 == qu.readconf(k, v))
				continue;
			gui.readconf(k, v);
		}

		dbglog(TAG, "loadconf: %s: %s", fn, bs);
	}

	void errlog(String mod, String fmt, Object... args) {
		if (BuildConfig.DEBUG)
			Log.e(mod, String.format("%s: %s", mod, String.format(fmt, args)));
		if (gui != null)
			gui.on_error(fmt, args);
	}

	void dbglog(String mod, String fmt, Object... args) {
		if (BuildConfig.DEBUG)
			Log.d(mod, String.format("%s: %s", mod, String.format(fmt, args)));
	}
}

abstract class CoreBase {
	private static final String TAG = "Core";

	abstract void errlog(String mod, String fmt, Object... args);

	int str_to_int(String s, int def) {
		try {
			return Integer.decode(s);
		} catch (Exception e) {
			return def;
		}
	}

	boolean str_to_bool(String s) {
		return s.equals("1");
	}

	int bool_to_int(boolean b) {
		if (b)
			return 1;
		return 0;
	}

	/**
	 * Get internal and external sdcard paths
	 */
	String[] system_storage_dirs(Context ctx) {
		ArrayList<String> a = new ArrayList<>();
		File[] dirs = ContextCompat.getExternalFilesDirs(ctx, null);
		for (File dir : dirs) {
			if (dir != null) {
				String path = dir.getAbsolutePath(); // "/STORAGE_PATH.../Android/data/..."
				int i = path.indexOf("/Android/data/");
				if (i >= 0)
					a.add(path.substring(0, i));
			}
		}
		return a.toArray(new String[0]);
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
		try {
			File f = new File(old);
			return f.renameTo(new File(newname));
		} catch (Exception e) {
			errlog(TAG, "file_rename: %s", e);
			return false;
		}
	}

	boolean file_delete(String path) {
		try {
			File f = new File(path);
			return f.delete();
		} catch (Exception e) {
			errlog(TAG, "file_rename: %s", e);
			return false;
		}
	}

	boolean file_exists(String path) {
		try {
			File f = new File(path);
			return f.exists();
		} catch (Exception e) {
			return true;
		}
	}

	boolean dir_make(String path) {
		try {
			File f = new File(path);
			return f.mkdir();
		} catch (Exception e) {
			errlog(TAG, "file_rename: %s", e);
			return false;
		}
	}

	/**
	 * Find string in array (case-insensitive)
	 */
	int array_ifind(String[] array, String search) {
		for (int i = 0; i != array.length; i++) {
			if (search.equalsIgnoreCase(array[i]))
				return i;
		}
		return -1;
	}
}

class Splitter {
	private int off;

	/**
	 * Return null if no more entries.
	 */
	String next(String s, char by) {
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
