/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import androidx.annotation.NonNull;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.Environment;
import android.util.Log;

class CoreSettings {
	private Core core;
	boolean svc_notification_disable;
	String trash_dir;
	String rec_path = ""; // directory for recordings
	int enc_bitrate = 192; // encoder bitrate
	boolean file_del;
	boolean no_tags;

	CoreSettings(Core core) {
		this.core = core;
		trash_dir = String.format("%s/Trash", core.storage_path);
	}

	@SuppressLint("DefaultLocale")
	String writeconf() {
		return String.format("svc_notification_disable %d\n", core.bool_to_int(svc_notification_disable)) +
				String.format("file_delete %d\n", core.bool_to_int(file_del)) +
				String.format("no_tags %d\n", core.bool_to_int(no_tags)) +
				String.format("rec_path %s\n", rec_path) +
				String.format("enc_bitrate %d\n", enc_bitrate) +
				String.format("trash_dir %s\n", trash_dir);
	}

	int readconf(String k, String v) {
		if (k.equals("svc_notification_disable"))
			svc_notification_disable = core.str_to_bool(v);
		else if (k.equals("rec_path"))
			rec_path = v;
		else if (k.equals("file_delete"))
			file_del = core.str_to_bool(v);
		else if (k.equals("trash_dir"))
			trash_dir = v;
		else if (k.equals("no_tags"))
			no_tags = core.str_to_bool(v);
		else if (k.equals("enc_bitrate"))
			enc_bitrate = core.str_to_uint(v, enc_bitrate);
		else
			return 1;
		return 0;
	}
}

class Core extends Util {
	private static Core instance;
	private int refcount;

	private static final String TAG = "fmedia.Core";
	private final String CONF_FN = "fmedia-user.conf";

	private GUI gui;
	private Queue qu;
	private Track track;
	private SysJobs sysjobs;
	private MP mp;
	Fmedia fmedia;

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

		fmedia = new Fmedia();
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
		fmedia.destroy();
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
