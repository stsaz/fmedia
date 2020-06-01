package com.github.stsaz.fmedia;

import android.content.Context;
import android.widget.Toast;

class GUI {
	private Core core;
	Context cur_activity;
	boolean filter_show = true;
	boolean record_show = true;
	String cur_path = ""; // current explorer path
	String rec_path = ""; // directory for recordings
	int list_pos; // list scroll position

	GUI(Core core) {
		this.core = core;
	}

	/*
	curpath /path
	rec_path /path
	filter_show true|false
	record_show true|false
	list_pos 123
	*/
	String writeconf() {
		StringBuilder s = new StringBuilder();
		s.append(String.format("curpath %s\n", cur_path));
		s.append(String.format("rec_path %s\n", rec_path));
		s.append(String.format("filter_show %s\n", core.str_frombool(filter_show)));
		s.append(String.format("record_show %s\n", core.str_frombool(record_show)));
		s.append(String.format("list_pos %d\n", list_pos));
		return s.toString();
	}

	int readconf(String k, String v) {
		if (k.equals("curpath"))
			cur_path = v;
		else if (k.equals("rec_path"))
			rec_path = v;
		else if (k.equals("filter_show"))
			filter_show = core.str_tobool(v);
		else if (k.equals("record_show"))
			record_show = core.str_tobool(v);
		else if (k.equals("list_pos"))
			list_pos = core.str_toint(v, 0);
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
