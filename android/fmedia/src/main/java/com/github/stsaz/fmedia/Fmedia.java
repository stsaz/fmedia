/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

class Fmedia {
	public Fmedia() {
		length_msec = 0;
		artist = "";
		title = "";
		init();
	}
	private native void init();
	public native void destroy();

	public native void setCodepage(String codepage);

	long length_msec;
	String artist, title;
	public native String[] meta(String filepath);

	static final int F_DATE_PRESERVE = 1;
	static final int F_OVERWRITE = 2;
	static final int F_TRASH_ORIG = 4;
	String trash_dir;
	String from_msec, to_msec;
	boolean copy;
	int aac_quality;
	String result;
	native int convert(String iname, String oname, int flags);

	public native String[] listDirRecursive(String filepath);

	public native String[] playlistLoadData(byte[] data);
	public native String[] playlistLoad(String filepath);
	public native boolean playlistSave(String filepath, String[] list);

	public native String trash(String trash_dir, String filepath);

	static {
		System.loadLibrary("fdk-aac-phi");
		System.loadLibrary("FLAC-phi");
		System.loadLibrary("mpg123-phi");
		System.loadLibrary("fmedia");
	}
}
