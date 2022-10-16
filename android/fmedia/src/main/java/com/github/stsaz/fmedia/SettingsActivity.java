package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.TextView;

import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

public class SettingsActivity extends AppCompatActivity {
	private static final String TAG = "SettingsActivity";
	private Core core;
	private SwitchCompat brandom, brepeat, bfilter_hide, brec_hide, bsvc_notif_disable, bfile_del;
	private TextView trecdir, tbitrate, ttrash_dir;

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
		brandom = findViewById(R.id.brandom);
		brandom.setChecked(core.queue().is_random());

		brepeat = findViewById(R.id.brepeat);
		brepeat.setChecked(core.queue().is_repeat());

		bfilter_hide = findViewById(R.id.bshowfilter);
		bfilter_hide.setChecked(core.gui().filter_hide);

		brec_hide = findViewById(R.id.bshowrec);
		brec_hide.setChecked(core.gui().record_hide);

		bsvc_notif_disable = findViewById(R.id.bsvc_notif_disable);
		bsvc_notif_disable.setChecked(core.setts.svc_notification_disable);

		trecdir = findViewById(R.id.trecdir);
		trecdir.setText(core.gui().rec_path);

		tbitrate = findViewById(R.id.tbitrate);
		tbitrate.setText(Integer.toString(core.gui().enc_bitrate));

		ttrash_dir = findViewById(R.id.ttrash_dir);
		ttrash_dir.setText(core.setts.trash_dir);

		bfile_del = findViewById(R.id.bfile_del);
		bfile_del.setChecked(core.setts.file_del);
	}

	private void save() {
		core.queue().random(brandom.isChecked());
		core.queue().repeat(brepeat.isChecked());
		core.setts.svc_notification_disable = bsvc_notif_disable.isChecked();
		core.setts.trash_dir = ttrash_dir.getText().toString();
		core.setts.file_del = bfile_del.isChecked();
		core.gui().filter_hide = bfilter_hide.isChecked();
		core.gui().record_hide = brec_hide.isChecked();
		core.gui().rec_path = trecdir.getText().toString();
		core.gui().enc_bitrate = core.str_to_int(tbitrate.getText().toString(), core.gui().enc_bitrate);
	}
}