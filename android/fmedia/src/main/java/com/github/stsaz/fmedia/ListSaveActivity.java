/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.Button;
import android.widget.EditText;

import androidx.appcompat.app.AppCompatActivity;

public class ListSaveActivity extends AppCompatActivity {
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
		core.queue().save(fn);
		this.finish();
	}
}
