/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.TextView;

import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

public class SettingsActivity extends AppCompatActivity {
	private static final String TAG = "fmedia.SettingsActivity";
	private Core core;
	private SwitchCompat brandom, brepeat, bfilter_hide, brec_hide, bsvc_notif_disable, bfile_del;
	private SwitchCompat bnotags, bdark;
	private TextView trecdir, tbitrate, ttrash_dir, tautoskip, tcodepage;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.settings);
		ActionBar actionBar = getSupportActionBar();
		if (actionBar != null)
			actionBar.setDisplayHomeAsUpEnabled(true);

		core = Core.getInstance();
		load();
	}

	@Override
	protected void onPause() {
		core.dbglog(TAG, "onPause");
		save();
		super.onPause();
	}

	@Override
	protected void onDestroy() {
		core.dbglog(TAG, "onDestroy");
		core.unref();
		super.onDestroy();
	}

	private void load() {
		// Interface
		bdark = findViewById(R.id.bdark);
		bdark.setChecked(core.gui().theme == GUI.THM_DARK);

		bfilter_hide = findViewById(R.id.bshowfilter);
		bfilter_hide.setChecked(core.gui().filter_hide);

		brec_hide = findViewById(R.id.bshowrec);
		brec_hide.setChecked(core.gui().record_hide);

		bsvc_notif_disable = findViewById(R.id.bsvc_notif_disable);
		bsvc_notif_disable.setChecked(core.setts.svc_notification_disable);

		// Playback
		brandom = findViewById(R.id.brandom);
		brandom.setChecked(core.queue().is_random());

		brepeat = findViewById(R.id.brepeat);
		brepeat.setChecked(core.queue().is_repeat());

		bnotags = findViewById(R.id.bnotags);
		bnotags.setChecked(core.setts.no_tags);

		tcodepage = findViewById(R.id.tcodepage);
		tcodepage.setText(core.setts.codepage);

		tautoskip = findViewById(R.id.tautoskip);
		tautoskip.setText(Integer.toString(core.queue().autoskip_msec / 1000));

		// Operation
		ttrash_dir = findViewById(R.id.ttrash_dir);
		ttrash_dir.setText(core.setts.trash_dir);

		bfile_del = findViewById(R.id.bfile_del);
		bfile_del.setChecked(core.setts.file_del);

		// Recording
		trecdir = findViewById(R.id.trecdir);
		trecdir.setText(core.setts.rec_path);

		tbitrate = findViewById(R.id.tbitrate);
		tbitrate.setText(Integer.toString(core.setts.enc_bitrate));
	}

	private void save() {
		core.queue().random(brandom.isChecked());
		core.queue().repeat(brepeat.isChecked());
		core.setts.no_tags = bnotags.isChecked();
		core.setts.set_codepage(tcodepage.getText().toString());
		core.fmedia.setCodepage(core.setts.codepage);
		core.queue().autoskip_msec = core.str_to_uint(tautoskip.getText().toString(), 0) * 1000;

		core.setts.svc_notification_disable = bsvc_notif_disable.isChecked();
		core.setts.trash_dir = ttrash_dir.getText().toString();
		core.setts.file_del = bfile_del.isChecked();

		int i = GUI.THM_DEF;
		if (bdark.isChecked())
			i = GUI.THM_DARK;
		core.gui().theme = i;

		core.gui().filter_hide = bfilter_hide.isChecked();
		core.gui().record_hide = brec_hide.isChecked();
		core.setts.rec_path = trecdir.getText().toString();
		core.setts.enc_bitrate = core.str_to_uint(tbitrate.getText().toString(), core.setts.enc_bitrate);
	}
}
