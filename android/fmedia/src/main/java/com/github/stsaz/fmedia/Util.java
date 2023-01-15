/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.content.Context;

import androidx.core.content.ContextCompat;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;

abstract class Util {
	private static final String TAG = "fmedia.Util";

	abstract void errlog(String mod, String fmt, Object... args);

	String int_to_str(int v) {
		return String.format("%d", v);
	}

	String float_to_str(float v) {
		return String.format("%.02f", v);
	}

	float str_to_float(String s, float def) {
		try {
			return Float.parseFloat(s);
		} catch (Exception e) {
		}
		return def;
	}

	int str_to_uint(String s, int def) {
		try {
			int i = Integer.decode(s);
			if (i >= 0)
				return i;
		} catch (Exception e) {
		}
		return def;
	}

	int str_to_int(String s, int def) {
		try {
			return Integer.decode(s);
		} catch (Exception e) {
		}
		return def;
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

	/**
	 * Read all data from InputStream
	 */
	byte[] istream_readall(InputStream is) {
		ByteArrayOutputStream b = new ByteArrayOutputStream();
		byte[] d = new byte[4096];
		try {
			for (; ; ) {
				int r = is.read(d);
				if (r <= 0)
					break;
				b.write(d, 0, r);
			}
		} catch (Exception e) {
			errlog(TAG, "istream_readall(): %s", e);
			b.reset();
		}
		return b.toByteArray();
	}

	/**
	 * Split full file path into path (without slash) and file name
	 */
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
}
