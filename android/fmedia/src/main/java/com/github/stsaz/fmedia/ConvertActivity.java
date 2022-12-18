/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.SwitchCompat;

public class ConvertActivity extends AppCompatActivity {
	Core core;
	private EditText tiname, toname, toext, tfrom, tuntil, taac_q;
	private SwitchCompat bcopy, bpreserve;
	private Button bstart;
	private TextView lresult;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.convert);

		core = Core.getInstance();

		tiname = findViewById(R.id.conv_tiname);
		toname = findViewById(R.id.conv_toname);
		toext = findViewById(R.id.conv_toext);
		tfrom = findViewById(R.id.conv_tfrom);
		tuntil = findViewById(R.id.conv_tuntil);
		bcopy = findViewById(R.id.conv_bcopy);
		bpreserve = findViewById(R.id.conv_bpreserve_date);
		taac_q = findViewById(R.id.conv_taac_q);
		bstart = findViewById(R.id.conv_bstart);
		bstart.setOnClickListener((v) -> convert());
		lresult = findViewById(R.id.conv_lresult);

		load();

		String iname = getIntent().getStringExtra("iname");
		tiname.setText(iname);
		int pos = iname.lastIndexOf('.');
		if (pos < 0)
			pos = 0;
		toname.setText(iname.substring(0, pos));

		if (true) {
			int from_sec = getIntent().getIntExtra("from", 0);
			int until_sec = getIntent().getIntExtra("length", 0);
			tfrom.setText(String.format("%d:%02d", from_sec/60, from_sec%60));
			tuntil.setText(String.format("%d:%02d", until_sec/60, until_sec%60));
		}
	}

	void load() {
		toext.setText(core.setts.conv_outext);
		taac_q.setText(core.int_to_str(core.setts.conv_aac_quality));
		bcopy.setChecked(core.setts.conv_copy);
	}

	void save() {
		core.setts.conv_outext = toext.getText().toString();
		core.setts.conv_copy = bcopy.isChecked();
		int v = core.str_to_uint(taac_q.getText().toString(), 0);
		if (v != 0)
			core.setts.conv_aac_quality = v;
	}

	@Override
	protected void onDestroy() {
		save();
		core.unref();
		super.onDestroy();
	}

	private void convert() {
		lresult.setText("Working...");

		int flags = 0;
		if (bpreserve.isChecked())
			flags |= Fmedia.F_DATE_PRESERVE;
		if (false)
			flags |= Fmedia.F_OVERWRITE;

		core.fmedia.from_msec = tfrom.getText().toString();
		core.fmedia.to_msec = tuntil.getText().toString();
		core.fmedia.copy = bcopy.isChecked();
		core.fmedia.aac_quality = core.str_to_uint(taac_q.getText().toString(), 0);
		String r = core.fmedia.convert(
				tiname.getText().toString(),
				String.format("%s.%s", toname.getText().toString(), toext.getText().toString()),
				flags
		);
		lresult.setText(r);
	}
}
