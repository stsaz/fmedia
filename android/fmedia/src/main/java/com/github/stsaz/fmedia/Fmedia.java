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

	static int F_DATE_PRESERVE = 1;
	static int F_OVERWRITE = 2;
	native String streamCopy(String iname, String oname, String from_msec, String to_msec, int flags);

	public native String[] listDirRecursive(String filepath);

	public native String[] playlistLoadData(byte[] data);
	public native String[] playlistLoad(String filepath);
	public native boolean playlistSave(String filepath, String[] list);

	static {
		System.loadLibrary("fmedia");
	}
}
