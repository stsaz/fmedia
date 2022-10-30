/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

class Fmedia {
	public Fmedia() {
		init();
	}
	private native void init();

	public native String[] meta(String filepath);

	public native String[] playlistLoad(String filepath);
	public native boolean playlistSave(String filepath, String[] list);

	static {
		System.loadLibrary("fmedia");
	}
}
