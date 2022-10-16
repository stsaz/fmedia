package com.github.stsaz.fmedia;

import android.annotation.SuppressLint;
import android.content.Context;
import android.widget.Toast;

class GUI {
	private Core core;
	Context cur_activity;
	boolean filter_hide;
	boolean record_hide;
	String cur_path = ""; // current explorer path
	String rec_path = ""; // directory for recordings
	int list_pos; // list scroll position
	int enc_bitrate = 192; // encoder bitrate

	GUI(Core core) {
		this.core = core;
	}

	/*
	curpath /path
	rec_path /path
	filter_hide 0|1
	record_hide 0|1
	list_pos 123
	enc_bitrate 123
	*/
	@SuppressLint("DefaultLocale")
	String writeconf() {
		return String.format("curpath %s\n", cur_path) +
				String.format("rec_path %s\n", rec_path) +
				String.format("filter_hide %d\n", core.bool_to_int(filter_hide)) +
				String.format("record_hide %d\n", core.bool_to_int(record_hide)) +
				String.format("list_pos %d\n", list_pos) +
				String.format("enc_bitrate %d\n", enc_bitrate);
	}

	int readconf(String k, String v) {
		if (k.equals("curpath"))
			cur_path = v;
		else if (k.equals("rec_path"))
			rec_path = v;
		else if (k.equals("enc_bitrate"))
			enc_bitrate = core.str_to_int(v, enc_bitrate);
		else if (k.equals("filter_hide"))
			filter_hide = core.str_to_bool(v);
		else if (k.equals("record_hide"))
			record_hide = core.str_to_bool(v);
		else if (k.equals("list_pos"))
			list_pos = core.str_to_int(v, 0);
		else
			return 1;
		return 0;
	}

	void on_error(String fmt, Object... args) {
		if (cur_activity == null)
			return;
		msg_show(cur_activity, fmt, args);
	}

	/**
	 * Show status message to the user.
	 */
	void msg_show(Context ctx, String fmt, Object... args) {
		Toast.makeText(ctx, String.format(fmt, args), Toast.LENGTH_SHORT).show();
	}
}
