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

	long length_msec;
	String artist, title;
	public native String[] meta(String filepath);

	public native String[] listDirRecursive(String filepath);

	public native String[] playlistLoadData(byte[] data);
	public native String[] playlistLoad(String filepath);
	public native boolean playlistSave(String filepath, String[] list);

	static {
		System.loadLibrary("fmedia");
	}
}
