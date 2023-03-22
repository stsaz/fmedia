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
	private static final String TAG = "fmedia.ConvertActivity";
	Core core;
	private EditText tiname, todir, toname, toext, tfrom, tuntil, tsample_rate, taac_q;
	private SwitchCompat bcopy, bpreserve, btrash_orig, bpl_add;
	private Button bfrom_set_cur, until_set_cur, bstart;
	private TextView lresult;
	private int qu_cur_pos;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.convert);

		core = Core.getInstance();

		tiname = findViewById(R.id.conv_tiname);
		todir = findViewById(R.id.conv_todir);
		toname = findViewById(R.id.conv_toname);
		toext = findViewById(R.id.conv_toext);
		tfrom = findViewById(R.id.conv_tfrom);
		bfrom_set_cur = findViewById(R.id.conv_bfrom_set_cur);
		bfrom_set_cur.setOnClickListener((v) -> pos_set_cur(true));
		until_set_cur = findViewById(R.id.conv_until_set_cur);
		until_set_cur.setOnClickListener((v) -> pos_set_cur(false));
		tuntil = findViewById(R.id.conv_tuntil);
		bcopy = findViewById(R.id.conv_bcopy);
		bpreserve = findViewById(R.id.conv_bpreserve_date);
		btrash_orig = findViewById(R.id.conv_btrash_orig);
		bpl_add = findViewById(R.id.conv_bpl_add);
		tsample_rate = findViewById(R.id.conv_tsample_rate);
		taac_q = findViewById(R.id.conv_taac_q);
		bstart = findViewById(R.id.conv_bstart);
		bstart.setOnClickListener((v) -> convert());
		lresult = findViewById(R.id.conv_lresult);

		load();

		qu_cur_pos = core.queue().cur();
		String iname = getIntent().getStringExtra("iname");
		tiname.setText(iname);

		int sl = iname.lastIndexOf('/');
		if (sl < 0)
			sl = 0;
		else
			sl++;
		todir.setText(iname.substring(0, sl));

		int pos = iname.lastIndexOf('.');
		if (pos < 0)
			pos = 0;
		toname.setText(iname.substring(sl, pos));
	}

	private void load() {
		toext.setText(core.setts.conv_outext);
		taac_q.setText(core.int_to_str(core.setts.conv_aac_quality));
		bcopy.setChecked(core.setts.conv_copy);
	}

	private void save() {
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

	/** Set 'from/until' position equal to the current playing position */
	private void pos_set_cur(boolean from) {
		int sec = core.track().curpos_msec() / 1000;
		String s = String.format("%d:%02d", sec/60, sec%60);
		if (from)
			tfrom.setText(s);
		else
			tuntil.setText(s);
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
		core.fmedia.sample_rate = core.str_to_uint(tsample_rate.getText().toString(), 0);
		core.fmedia.aac_quality = core.str_to_uint(taac_q.getText().toString(), 0);
		String iname = tiname.getText().toString();
		String oname = String.format("%s/%s.%s", todir.getText().toString(), toname.getText().toString(), toext.getText().toString());
		int r = core.fmedia.convert(iname, oname, flags);
		lresult.setText(core.fmedia.result);

		if (r == 0 && bpl_add.isChecked()) {
			core.queue().add(oname);
		}

		if (r == 0 && btrash_orig.isChecked() && !iname.equals(oname)) {
			trash_input_file(iname);
		}
	}

	private void trash_input_file(String iname) {
		String e = core.fmedia.trash(core.setts.trash_dir, iname);
		if (!e.isEmpty()) {
			core.errlog(TAG, "Can't trash file %s: %s", iname, e);
			return;
		}

		if (qu_cur_pos == -1) return;

		String url = core.queue().get(qu_cur_pos);
		if (url.equals(iname))
			core.queue().remove(qu_cur_pos);
		qu_cur_pos = -1;
	}
}
