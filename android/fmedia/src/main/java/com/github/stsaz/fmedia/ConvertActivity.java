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
	private EditText tiname, toname, tfrom, tuntil;
	private SwitchCompat bpreserve;
	private Button bstart;
	private TextView lresult;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.convert);

		String iname = getIntent().getStringExtra("iname");
		int from_sec = getIntent().getIntExtra("from", 0);
		int until_sec = getIntent().getIntExtra("length", 0);

		core = Core.getInstance();

		tiname = findViewById(R.id.conv_tiname);
		tiname.setText(iname);

		toname = findViewById(R.id.conv_toname);
		toname.setText(iname);

		tfrom = findViewById(R.id.conv_tfrom);
		tfrom.setText(String.format("%d:%02d", from_sec/60, from_sec%60));

		tuntil = findViewById(R.id.conv_tuntil);
		tuntil.setText(String.format("%d:%02d", until_sec/60, until_sec%60));

		bpreserve = findViewById(R.id.conv_bpreserve_date);

		bstart = findViewById(R.id.conv_bstart);
		bstart.setOnClickListener((v) -> convert());

		lresult = findViewById(R.id.conv_lresult);
	}

	@Override
	protected void onDestroy() {
		core.unref();
		super.onDestroy();
	}

	private void convert() {
		lresult.setText("Working...");

		int flags = 0;
		if (bpreserve.isChecked())
			flags |= Fmedia.F_DATE_PRESERVE;

		String r = core.fmedia.streamCopy(
				tiname.getText().toString(), toname.getText().toString(),
				tfrom.getText().toString(), tuntil.getText().toString(),
				flags
		);
		lresult.setText(r);
	}
}
