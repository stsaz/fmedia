/**
 * fmedia/Android
 * 2022, Simon Zolin
 */

package com.github.stsaz.fmedia;

import android.os.Bundle;
import android.widget.ArrayAdapter;
import android.widget.ListView;

import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AppCompatActivity;

import java.util.ArrayList;

public class TagsActivity extends AppCompatActivity  {
	private Core core;
	private ListView lv_tags;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.tags);
		ActionBar actionBar = getSupportActionBar();
		if (actionBar != null)
			actionBar.setDisplayHomeAsUpEnabled(true);

		lv_tags = findViewById(R.id.lv_tags);

		core = Core.getInstance();
		show();
	}

	@Override
	protected void onDestroy() {
		core.unref();
		super.onDestroy();
	}

	private void show() {
		String[] meta = core.track().meta();
		if (meta == null)
			meta = new String[0];
		ArrayList<String> tags = new ArrayList<>();
		for (int i = 0; i < meta.length; i+=2) {
			tags.add(String.format("%s : %s", meta[i], meta[i+1]));
		}
		ArrayAdapter<String> adapter = new ArrayAdapter<>(this, R.layout.list_row, tags);
		lv_tags.setAdapter(adapter);
	}
}
