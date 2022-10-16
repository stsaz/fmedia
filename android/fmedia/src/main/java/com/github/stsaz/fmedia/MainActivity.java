package com.github.stsaz.fmedia;

import android.Manifest;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.ToggleButton;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;
import androidx.appcompat.widget.SearchView;
import androidx.core.app.ActivityCompat;

import java.io.File;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Comparator;
import java.util.Date;
import java.util.GregorianCalendar;

public class MainActivity extends AppCompatActivity {
	private static final String TAG = "MainActivity";
	private static final int REQUEST_PERM_READ_STORAGE = 1;
	static final int REQUEST_STORAGE_ACCESS = 1;
	private static final int REQUEST_SAVE_FILE = 2;
	private static final int REQUEST_OPEN_FILE = 3;

	private Core core;
	private GUI gui;
	private Queue queue;
	private QueueNotify quenfy;
	private Track track;
	private Filter trk_nfy;
	private TrackCtl trackctl;
	private int total_dur_msec;
	private int state;
	private int cur_view; // Explorer/Playlist view switch (0:Playlist)
	private Explorer explorer;

	private TrackHandle trec;

	// Playlist track filter:
	private int filter_strlen;
	private ArrayList<Integer> filtered_idx;
	private ArrayAdapter<String> filter_adapter;

	private TextView lbl_name;
	private ImageButton brec;
	private ImageButton bplay;
	private TextView lbl_pos;
	private ListView list;
	private SeekBar progs;
	private ToggleButton bexplorer;
	private ToggleButton bplist;
	private SearchView tfilter;

	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.main);

		Toolbar toolbar = findViewById(R.id.toolbar);
		setSupportActionBar(toolbar);

		if (0 != init_mods())
			return;
		init_system();
		init_ui();

		plist_show();
		list.setSelection(gui.list_pos);

		if (gui.cur_path.isEmpty())
			gui.cur_path = core.storage_path;

		if (gui.rec_path.isEmpty())
			gui.rec_path = core.storage_path + "/Recordings";
	}

	protected void onStart() {
		super.onStart();
		core.dbglog(TAG, "onStart()");

		show_ui();

		// If already playing - get in sync
		track.filter_notify(trk_nfy);
	}

	protected void onResume() {
		super.onResume();
		if (core != null) {
			core.dbglog(TAG, "onResume()");
		}
	}

	protected void onStop() {
		if (core != null) {
			core.dbglog(TAG, "onStop()");
			queue.saveconf();
			if (cur_view == 0)
				gui.list_pos = list.getFirstVisiblePosition();
			core.saveconf();
		}
		super.onStop();
	}

	public void onDestroy() {
		if (core != null) {
			core.dbglog(TAG, "onDestroy()");
			track.filter_rm(trk_nfy);
			trackctl.close();
			queue.nfy_rm(quenfy);
			core.close();
		}
		super.onDestroy();
	}

	public boolean onCreateOptionsMenu(Menu menu) {
		Toolbar tb = findViewById(R.id.toolbar);
		tb.inflateMenu(R.menu.menu);
		tb.setOnMenuItemClickListener((MenuItem item) -> {
					return onOptionsItemSelected(item);
				}
		);
		return true;
	}

	public boolean onOptionsItemSelected(@NonNull MenuItem item) {
		switch (item.getItemId()) {
			case R.id.action_settings:
				startActivity(new Intent(this, SettingsActivity.class));
				return true;

			case R.id.action_list_rm:
				list_rm();
				return true;

			case R.id.action_file_del:
				file_del_cur();
				return true;

			case R.id.action_list_add:
				startActivity(new Intent(this, AddURLActivity.class));
				return true;

			case R.id.action_list_clear:
				queue.clear();
				plist_show2();
				return true;

			case R.id.action_list_save:
				list_save();
				return true;

			case R.id.action_list_load:
				list_load();
				return true;

			case R.id.action_list_showcur: {
				plist_click();
				int pos = queue.cur();
				if (pos >= 0)
					list.setSelection(pos);
				return true;
			}

			case R.id.action_list_showcur_explorer: {
				int pos = queue.cur();
				if (pos >= 0) {
					pos = explorer.show_cur(pos);
					if (pos >= 0)
						list.setSelection(pos);
				}
				return true;
			}

			case R.id.action_about:
				startActivity(new Intent(this, AboutActivity.class));
				return true;
		}
		return super.onOptionsItemSelected(item);
	}

	/**
	 * Called by OS with the result of requestPermissions().
	 */
	public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
		if (grantResults.length != 0)
			core.dbglog(TAG, "onRequestPermissionsResult: %d: %d", requestCode, grantResults[0]);
		/*switch (requestCode) {
			case PERMREQ_READ_EXT_STORAGE:
				if (grantResults.length != 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
				}
		}*/
	}

	public void onActivityResult(int requestCode, int resultCode, Intent data) {
		super.onActivityResult(requestCode, resultCode, data);

		if (resultCode != RESULT_OK) {
			core.errlog(TAG, "onActivityResult: requestCode:%d resultCode:%d", requestCode, resultCode);
			return;
		}

		switch (requestCode) {
			case REQUEST_STORAGE_ACCESS:
				if (Environment.isExternalStorageManager()) {
				}
				break;

			case REQUEST_SAVE_FILE:
				try {
					OutputStream os = getContentResolver().openOutputStream(data.getData());
					core.queue().save_stream(os);
					core.gui().msg_show(this, "Saved playlist file");
				} catch (Exception e) {
					core.errlog(TAG, "openOutputStream(): %s", e);
				}
				break;

			case REQUEST_OPEN_FILE:
				try {
					InputStream is = getContentResolver().openInputStream(data.getData());
					core.queue().clear();
					core.queue().load_data(is.readAllBytes());
					plist_show();
				} catch (Exception e) {
					core.errlog(TAG, "openInputStream(): %s", e);
				}
				break;
		}
	}

	/**
	 * Request system permissions
	 */
	private void init_system() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
			String[] perms = new String[]{
					Manifest.permission.READ_EXTERNAL_STORAGE,
					Manifest.permission.WRITE_EXTERNAL_STORAGE,
					Manifest.permission.RECORD_AUDIO
			};
			for (String perm : perms) {
				if (ActivityCompat.checkSelfPermission(this, perm) != PackageManager.PERMISSION_GRANTED) {
					core.dbglog(TAG, "ActivityCompat.requestPermissions");
					ActivityCompat.requestPermissions(this, perms, REQUEST_PERM_READ_STORAGE);
					break;
				}
			}
		}
	}

	/**
	 * Initialize core and modules
	 */
	private int init_mods() {
		core = Core.init_once(getApplicationContext());
		if (core == null)
			return -1;
		core.dbglog(TAG, "init_mods()");

		gui = core.gui();
		queue = core.queue();
		quenfy = new QueueNotify() {
			public void on_change(int what) {
				plist_show2();
			}
		};
		queue.nfy_add(quenfy);
		track = core.track();
		trk_nfy = new Filter() {
			public int open(TrackHandle t) {
				return new_track(t);
			}

			public void close(TrackHandle t) {
				close_track(t);
			}

			public int process(TrackHandle t) {
				return update_track(t);
			}
		};
		track.filter_add(trk_nfy);
		trackctl = new TrackCtl(core, this);
		trackctl.connect();
		trec = track.trec;
		return 0;
	}

	/**
	 * Set UI objects and register event handlers
	 */
	private void init_ui() {
		explorer = new Explorer(core, this);
		brec = findViewById(R.id.brec);
		brec.setOnClickListener((v) -> rec_click());

		bplay = findViewById(R.id.bplay);
		bplay.setOnClickListener((v) -> play_pause_click());

		findViewById(R.id.bnext).setOnClickListener((v) -> trackctl.next());

		findViewById(R.id.bprev).setOnClickListener((v) -> trackctl.prev());

		bexplorer = findViewById(R.id.bexplorer);
		bexplorer.setOnClickListener((v) -> explorer_click());

		bplist = findViewById(R.id.bplaylist);
		bplist.setOnClickListener((v) -> plist_click());
		bplist.setChecked(true);

		lbl_name = findViewById(R.id.lname);
		lbl_pos = findViewById(R.id.lpos);

		progs = findViewById(R.id.seekbar);
		progs.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
			int val; // last value

			public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
				if (fromUser)
					val = progress;
			}

			public void onStartTrackingTouch(SeekBar seekBar) {
				val = -1;
			}

			public void onStopTrackingTouch(SeekBar seekBar) {
				if (val != -1)
					seek(val);
			}
		});

		tfilter = findViewById(R.id.tfilter);
		tfilter.setOnQueryTextListener(new SearchView.OnQueryTextListener() {
			public boolean onQueryTextSubmit(String query) {
				return true;
			}

			public boolean onQueryTextChange(String newText) {
				plist_filter(newText);
				return true;
			}
		});

		list = findViewById(R.id.list);
		list.setOnItemClickListener((parent, view, position, id) -> list_click(position));
		list.setOnItemLongClickListener((parent, view, position, id) -> {
					return list_longclick(position);
				}
		);

		gui.cur_activity = this;
	}

	private void show_ui() {
		int v = View.VISIBLE;
		if (gui.record_hide)
			v = View.INVISIBLE;
		brec.setVisibility(v);

		v = View.VISIBLE;
		if (gui.filter_hide)
			v = View.INVISIBLE;
		tfilter.setVisibility(v);

		if (trec != null)
			brec.setImageResource(R.drawable.ic_rec_stop);
	}

	private void rec_click() {
		if (trec == null) {
			rec_start();
		} else {
			track.record_stop(trec);
			trec = null;
			brec.setImageResource(R.drawable.ic_rec);
			core.gui().msg_show(this, "Finished recording");
		}
	}

	private void play_pause_click() {
		if (track.state() == Track.STATE_PLAYING) {
			trackctl.pause();
		} else {
			trackctl.unpause();
		}
	}

	void explorer_click() {
		gui.list_pos = list.getFirstVisiblePosition();
		cur_view = -1;
		bexplorer.setChecked(true);
		bplist.setChecked(false);
		tfilter.setVisibility(View.INVISIBLE);

		String[] names;
		if (gui.cur_path.isEmpty())
			names = explorer.list_show_root();
		else
			names = explorer.list_show(gui.cur_path);
		list_set_data(names);
	}

	void plist_click() {
		cur_view = 0;
		bexplorer.setChecked(false);
		bplist.setChecked(true);
		if (!gui.filter_hide)
			tfilter.setVisibility(View.VISIBLE);
		plist_show2();
		list.setSelection(gui.list_pos);
	}

	/**
	 * Delete file and update view
	 */
	private void file_del(int pos, String fn) {
		if (!core.setts.file_del) {
			core.dir_make(core.setts.trash_dir);
			String trash_fn = String.format("%s/%s", core.setts.trash_dir, Splitter.path_split2(fn)[1]);
			if (core.file_exists(trash_fn)) {
				core.errlog(TAG, "Can't delete file: %s already exists in Trash", trash_fn);
				return;
			}
			if (!core.file_rename(fn, trash_fn))
				return;
			gui.msg_show(this, "Moved file to Trash directory");
		} else {
			if (!core.file_delete(fn))
				return;
			gui.msg_show(this, "Deleted file");
		}
		queue.remove(pos);
		plist_show2();
		list.setSelection(gui.list_pos);
	}

	/**
	 * Ask confirmation before deleting the currently playing file from storage
	 */
	private void file_del_cur() {
		int pos = queue.cur();
		if (pos < 0)
			return;
		final String fn = queue.list()[pos];

		AlertDialog.Builder b = new AlertDialog.Builder(this);
		b.setIcon(android.R.drawable.ic_dialog_alert);
		b.setTitle("File Delete");
		b.setMessage(String.format("Delete the currently playing file from storage: %s ?", fn));
		b.setPositiveButton("Delete", (dialog, which) -> file_del(pos, fn));
		b.setNegativeButton("Cancel", null);
		b.show();
	}

	/**
	 * UI event on listview click
	 * If the view is Playlist: start playing the track at this position.
	 * If the view is Explorer:
	 * if the entry is a directory: show its contents;
	 * or create a playlist with all the files in the current directory
	 * and start playing the selected track
	 */
	private void list_click(int pos) {
		if (cur_view == 0) {
			play(pos);
			return;
		}
		explorer.list_click(pos);
	}

	void list_set_data(String[] names) {
		ArrayAdapter<String> adapter = new ArrayAdapter<>(this, R.layout.list_row, names);
		list.setAdapter(adapter);
	}

	static final int PL_SET = 0;
	static final int PL_ADD = 1;

	void pl_set(String[] ents, int flags) {
		filter_strlen = 0;
		if (flags == PL_SET) {
			queue.clear_addmany(ents);
			core.dbglog(TAG, "added %d items", ents.length);
			gui.msg_show(this, "Set %d playlist items", ents.length);
		} else {
			queue.addmany(ents);
			core.dbglog(TAG, "added %d items", ents.length);
			gui.msg_show(this, "Added %d items to playlist", ents.length);
		}
	}

	private boolean list_longclick(int pos) {
		if (cur_view == 0)
			return false; // no action for a playlist
		return explorer.add_files_r(pos);
	}

	/**
	 * Show the playlist items
	 */
	private void plist_show() {
		if (cur_view < 0)
			return;
		filter_strlen = 0;
		String[] l = queue.list();
		ArrayList<String> names = new ArrayList<>();
		int i = 1;
		for (String s : l) {
			String[] path_name = Splitter.path_split2(s);
			names.add(String.format("%d. %s", i++, path_name[1]));
		}
		ArrayAdapter<String> adapter = new ArrayAdapter<>(this, R.layout.list_row, names.toArray(new String[0]));
		list.setAdapter(adapter);
	}

	private void plist_show2() {
		if (cur_view < 0)
			return;
		if (filter_strlen != 0)
			list.setAdapter(filter_adapter);
		else
			plist_show();
	}

	private void plist_filter(String filter) {
		if (cur_view < 0)
			return;
		core.dbglog(TAG, "list_filter: %s", filter);

		if (filter_strlen != 0 && filter.length() == 0) {
			filter_strlen = 0;
			plist_show();
			return;
		}

		boolean increase = (filter.length() > filter_strlen);
		if (filter.length() < 2) {
			if (!increase)
				plist_show();
			return;
		}

		filter = filter.toLowerCase();

		ArrayList<Integer> fi = new ArrayList<>();
		ArrayList<String> names = new ArrayList<>();
		String[] l = queue.list();
		int n = 1;

		if (filter_strlen == 0 || !increase) {
			for (int i = 0; i != l.length; i++) {
				String s = l[i];
				if (!s.toLowerCase().contains(filter))
					continue;
				String[] path_name = Splitter.path_split2(s);
				names.add(String.format("%d. %s", n++, path_name[1]));
				fi.add(i);
			}

		} else {
			for (Integer i : filtered_idx) {
				String s = l[i];
				if (!s.toLowerCase().contains(filter))
					continue;
				String[] path_name = Splitter.path_split2(s);
				names.add(String.format("%d. %s", n++, path_name[1]));
				fi.add(i);
			}
		}

		filter_adapter = new ArrayAdapter<>(this, R.layout.list_row, names.toArray(new String[0]));
		list.setAdapter(filter_adapter);
		filtered_idx = fi;
		filter_strlen = filter.length();
	}

	/**
	 * Remove currently playing track from playlist
	 */
	private void list_rm() {
		int pos = queue.cur();
		if (pos < 0)
			return;
		String fn = queue.list()[pos];
		queue.remove(pos);
		gui.msg_show(this, "Removed 1 entry");
		plist_show2();
		list.setSelection(gui.list_pos);
	}

	/**
	 * Show dialog for saving playlist file
	 */
	private void list_save() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			Intent it = new Intent(Intent.ACTION_CREATE_DOCUMENT);
			it.addCategory(Intent.CATEGORY_OPENABLE);
			it.setType("audio/x-mpegurl");
			it.putExtra(Intent.EXTRA_TITLE, "Playlist1.m3u8");
			ActivityCompat.startActivityForResult(this, it, REQUEST_SAVE_FILE, null);
			return;
		}

		core.errlog(TAG, "Feature not supported on your OS");
	}

	/**
	 * Show dialog for loading playlist file
	 */
	private void list_load() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT) {
			Intent it = new Intent(Intent.ACTION_OPEN_DOCUMENT);
			it.addCategory(Intent.CATEGORY_OPENABLE);
			it.setType("audio/x-mpegurl");
			ActivityCompat.startActivityForResult(this, it, REQUEST_OPEN_FILE, null);
			return;
		}

		core.errlog(TAG, "Feature not supported on your OS");
	}

	/**
	 * Start recording
	 */
	private void rec_start() {
		core.dir_make(gui.rec_path);
		Date d = new Date();
		Calendar cal = new GregorianCalendar();
		cal.setTime(d);
		int dt[] = {
				cal.get(Calendar.YEAR),
				cal.get(Calendar.MONTH),
				cal.get(Calendar.DAY_OF_MONTH) + 1,
				cal.get(Calendar.HOUR_OF_DAY),
				cal.get(Calendar.MINUTE),
				cal.get(Calendar.SECOND),
		};
		String fname = String.format("%s/rec_%04d%02d%02d_%02d%02d%02d.m4a"
				, gui.rec_path, dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);
		trec = track.record(fname);
		if (trec == null)
			return;
		brec.setImageResource(R.drawable.ic_rec_stop);
	}

	/**
	 * Start playing a new track
	 */
	void play(int pos) {
		if (filter_strlen != 0) {
			pos = filtered_idx.get(pos);
		}
		queue.play(pos);
	}

	/**
	 * UI event from seek bar
	 */
	private void seek(int percent) {
		trackctl.seek(percent * total_dur_msec / 100);
	}

	private static final int STATE_NONE = 0;
	private static final int STATE_PLAYING = 1;
	private static final int STATE_PAUSED = 2;

	private void state(int st) {
		if (st == state)
			return;
		switch (st) {
			case STATE_NONE:
				getSupportActionBar().setTitle("fmedia");
				bplay.setImageResource(R.drawable.ic_play);
				break;

			case STATE_PLAYING:
				getSupportActionBar().setTitle("fmedia");
				bplay.setImageResource(R.drawable.ic_pause);
				break;

			case STATE_PAUSED:
				getSupportActionBar().setTitle("[Paused] fmedia");
				bplay.setImageResource(R.drawable.ic_play);
				break;
		}
		core.dbglog(TAG, "state: %d -> %d", state, st);
		state = st;
	}

	/**
	 * Called by Track when a new track is initialized
	 */
	private int new_track(TrackHandle t) {
		lbl_name.setText(t.name);
		progs.setProgress(0);
		if (t.state == Track.STATE_PAUSED) {
			state(STATE_PAUSED);
		} else {
			state(STATE_PLAYING);
		}
		return 0;
	}

	/**
	 * Called by Track after a track is finished
	 */
	private void close_track(TrackHandle t) {
		lbl_name.setText("");
		lbl_pos.setText("");
		progs.setProgress(0);
		state(STATE_NONE);
	}

	/**
	 * Called by Track during playback
	 */
	private int update_track(TrackHandle t) {
		core.dbglog(TAG, "update_track: state:%d pos:%d", t.state, t.pos_msec);
		switch (t.state) {
			case Track.STATE_PAUSED:
				state(STATE_PAUSED);
				break;

			case Track.STATE_PLAYING:
				state(STATE_PLAYING);
				break;
		}

		int pos = t.pos_msec / 1000;
		total_dur_msec = t.time_total_msec;
		int dur = t.time_total_msec / 1000;
		int progress = 0;
		if (dur != 0)
			progress = pos * 100 / dur;
		progs.setProgress(progress);
		String s = String.format("%d:%02d / %d:%02d"
				, pos / 60, pos % 60, dur / 60, dur % 60);
		lbl_pos.setText(s);
		return 0;
	}
}
