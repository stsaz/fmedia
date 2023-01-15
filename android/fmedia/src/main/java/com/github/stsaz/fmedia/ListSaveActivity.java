/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;

import androidx.appcompat.app.AppCompatActivity;

import java.io.File;

public class ListSaveActivity extends AppCompatActivity {
	private static final String TAG = "fmedia.ListSaveActivity";
	Core core;
	private EditText tname;
	private Button bsave;

	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.list_save);

		core = Core.getInstance();

		tname = findViewById(R.id.lssv_tname);
		bsave = findViewById(R.id.lssv_bsave);
		bsave.setOnClickListener((v) -> save());

		tname.setText("Playlist1");
	}

	protected void onDestroy() {
		core.unref();
		super.onDestroy();
	}

	private void save() {
		String fn = String.format("%s/%s.m3u8", core.setts.pub_data_dir, tname.getText().toString());
		File f = new File(fn);
		if (f.exists()) {
			core.errlog(TAG, "File exists.  Please specify a different name.");
			return;
		}
		core.queue().save(fn);
		this.finish();
	}
}
