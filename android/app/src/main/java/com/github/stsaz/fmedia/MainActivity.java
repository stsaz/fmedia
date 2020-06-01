package com.github.stsaz.fmedia;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;
import androidx.core.app.ActivityCompat;

import android.Manifest;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.ToggleButton;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Calendar;
import java.util.Comparator;
import java.util.Date;
import java.util.GregorianCalendar;

public class MainActivity extends AppCompatActivity {
	private final String TAG = "UI";
	Core core;
	GUI gui;
	Queue queue;
	QueueNotify quenfy;
	Track track;
	Filter trk_nfy;
	TrackCtl trackctl;

	TrackHandle trec;

	// Explorer:
	String root_path; // upmost filesystem path
	String[] fns; // file names
	boolean updir; // "UP" directory link is shown
	int ndirs; // number of directories shown
	int cur_view; // Explorer/Playlist view switch (0:Playlist)
	ArrayList<String> tmp_fns; // temporary array for recursive directory contents

	int filter_strlen;
	ArrayList<Integer> filtered_idx;

	TextView lbl_name;
	ImageButton brec;
	ImageButton bplay;
	TextView lbl_pos;
	ListView list;
	SeekBar progs;
	ToggleButton bexplorer;
	ToggleButton bplist;
	EditText tfilter;

	enum Cmd {
		Rec,
		PlayPause,
		Next,
		Prev,
		Explorer,
		Playlist,
	}

	final int PERMREQ_READ_EXT_STORAGE = 1;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_main);

		Toolbar toolbar = findViewById(R.id.toolbar);
		setSupportActionBar(toolbar);

		if (0 != init_mods())
			return;
		init_system();
		init_ui();
		core.dbglog(TAG, "init");

		plist_show();
		list.setSelection(gui.list_pos);

		/* Prevent from going upper than sdcard because
		 it may be impossible to come back (due to file permissions) */
		root_path = Environment.getExternalStorageDirectory().getPath();
		if (gui.cur_path.length() == 0)
			gui.cur_path = root_path;
		if (gui.rec_path.length() == 0)
			gui.rec_path = root_path;
		bplist.setChecked(true);

		// If already playing - get in sync
		track.filter_notify(trk_nfy);
	}

	@Override
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

	@Override
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

	@Override
	public boolean onCreateOptionsMenu(Menu menu) {
		Toolbar tb = findViewById(R.id.toolbar);
		tb.inflateMenu(R.menu.menu);
		tb.setOnMenuItemClickListener(
				new Toolbar.OnMenuItemClickListener() {
					@Override
					public boolean onMenuItemClick(MenuItem item) {
						return onOptionsItemSelected(item);
					}
				});
		return true;
	}

	@Override
	public boolean onOptionsItemSelected(@NonNull MenuItem item) {
		switch (item.getItemId()) {
			case R.id.action_settings: {
				Intent intent = new Intent(this, SettingsActivity.class);
				startActivity(intent);
				return true;
			}

			case R.id.action_list_rm: {
				int pos = queue.cur();
				String fn = queue.list()[pos];
				queue.remove(pos);
				gui.msg_show(this, "Removed 1 entry");
				plist_show2();
				list.setSelection(gui.list_pos);
				return true;
			}

			case R.id.action_file_del: {
				final int pos = queue.cur();
				final String fn = queue.list()[pos];

				AlertDialog.Builder b = new AlertDialog.Builder(this);
				b.setIcon(android.R.drawable.ic_dialog_alert);
				b.setTitle("File Delete");
				b.setMessage(String.format("Delete the current file from storage: %s ?", fn));
				b.setPositiveButton("Delete", new DialogInterface.OnClickListener() {
					@Override
					public void onClick(DialogInterface dialog, int which) {
						file_del(pos, fn);
					}
				});
				b.setNegativeButton("Cancel", null);
				b.show();
				return true;
			}

			case R.id.action_list_add: {
				Intent intent = new Intent(this, AddURLActivity.class);
				startActivity(intent);
				return true;
			}

			case R.id.action_list_clear:
				queue.clear();
				plist_show2();
				return true;

			case R.id.action_list_showcur: {
				cmd(Cmd.Playlist);
				int pos = queue.cur();
				list.setSelection(pos);
				return true;
			}

			case R.id.action_list_showcur_explorer:
				explorer_showcur();
				return true;
		}
		return super.onOptionsItemSelected(item);
	}

	/**
	 * Called by OS with the result of requestPermissions().
	 */
	@Override
	public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
		if (grantResults.length != 0)
			core.dbglog(TAG, "onRequestPermissionsResult: %d: %d", requestCode, grantResults[0]);
		/*switch (requestCode) {
			case PERMREQ_READ_EXT_STORAGE:
				if (grantResults.length != 0
						&& grantResults[0] == PackageManager.PERMISSION_GRANTED) {
				}
		}*/
	}

	/**
	 * Request system permissions
	 */
	void init_system() {
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN) {
			String[] perms = new String[]{
					Manifest.permission.READ_EXTERNAL_STORAGE,
					Manifest.permission.WRITE_EXTERNAL_STORAGE,
					Manifest.permission.RECORD_AUDIO
			};
			for (String perm : perms) {
				if (ActivityCompat.checkSelfPermission(this, perm) != PackageManager.PERMISSION_GRANTED) {
					core.dbglog(TAG, "ActivityCompat.requestPermissions");
					ActivityCompat.requestPermissions(this, perms, PERMREQ_READ_EXT_STORAGE);
					break;
				}
			}
		}
	}

	/**
	 * Initialize core and modules
	 */
	int init_mods() {
		core = Core.init_once(getApplicationContext());
		if (core == null)
			return -1;
		gui = core.gui();
		queue = core.queue();
		quenfy = new QueueNotify() {
			@Override
			public void on_change(int what) {
				plist_show2();
			}
		};
		queue.nfy_add(quenfy);
		track = core.track();
		trk_nfy = new Filter() {
			@Override
			public int open(TrackHandle t) {
				return new_track(t);
			}

			@Override
			public void close(TrackHandle t) {
				close_track(t);
			}

			@Override
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
	void init_ui() {
		brec = findViewById(R.id.brec);
		brec.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				cmd(Cmd.Rec);
			}
		});
		if (!gui.record_show)
			brec.setVisibility(View.INVISIBLE);
		if (trec != null)
			brec.setImageResource(R.drawable.ic_stop);

		bplay = findViewById(R.id.bplay);
		bplay.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				cmd(Cmd.PlayPause);
			}
		});
		findViewById(R.id.bnext).setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				cmd(Cmd.Next);
			}
		});
		findViewById(R.id.bprev).setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				cmd(Cmd.Prev);
			}
		});
		bexplorer = findViewById(R.id.bexplorer);
		bexplorer.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				cmd(Cmd.Explorer);
			}
		});
		bplist = findViewById(R.id.bplaylist);
		bplist.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				cmd(Cmd.Playlist);
			}
		});

		lbl_name = findViewById(R.id.lname);
		lbl_pos = findViewById(R.id.lpos);

		progs = findViewById(R.id.seekbar);
		progs.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
			int val; // last value

			@Override
			public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
				if (fromUser)
					val = progress;
			}

			@Override
			public void onStartTrackingTouch(SeekBar seekBar) {
				val = -1;
			}

			@Override
			public void onStopTrackingTouch(SeekBar seekBar) {
				if (val != -1)
					seek(val);
			}
		});

		tfilter = findViewById(R.id.tfilter);
		if (gui.filter_show)
			tfilter.setVisibility(View.VISIBLE);
		tfilter.addTextChangedListener(new TextWatcher() {
			@Override
			public void beforeTextChanged(CharSequence s, int start, int count, int after) {
			}

			@Override
			public void onTextChanged(CharSequence s, int start, int before, int count) {
			}

			@Override
			public void afterTextChanged(Editable s) {
				list_filter(s.toString());
			}
		});

		list = findViewById(R.id.list);
		list.setOnItemClickListener(new AdapterView.OnItemClickListener() {
			@Override
			public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
				list_click(position);
			}
		});
		list.setOnItemLongClickListener(new AdapterView.OnItemLongClickListener() {
			@Override
			public boolean onItemLongClick(AdapterView<?> parent, View view, int position, long id) {
				return list_longclick(position);
			}
		});

		gui.cur_activity = this;
	}

	/**
	 * UI event on button click
	 */
	void cmd(Cmd c) {
		core.dbglog(TAG, "cmd: %s", c.name());
		switch (c) {
			case Rec:
				if (trec == null) {
					Date d = new Date();
					Calendar cal = new GregorianCalendar();
					cal.setTime(d);
					trec = track.record(String.format("%s/rec%04d%02d%02d_%02d%02d%02d.m4a"
							, gui.rec_path
							, cal.get(Calendar.YEAR), cal.get(Calendar.MONTH) + 1, cal.get(Calendar.DAY_OF_MONTH)
							, cal.get(Calendar.HOUR_OF_DAY), cal.get(Calendar.MINUTE), cal.get(Calendar.SECOND)));
					if (trec == null)
						break;
					brec.setImageResource(R.drawable.ic_stop);
				} else {
					track.record_stop(trec);
					trec = null;
					brec.setImageResource(R.drawable.ic_rec);
				}
				break;

			case PlayPause:
				if (track.state() == Track.State.PLAYING) {
					trackctl.pause();
					state(2);
				} else {
					trackctl.unpause();
					state(1);
				}
				break;

			case Next:
				trackctl.next();
				break;

			case Prev:
				trackctl.prev();
				break;

			case Explorer:
				gui.list_pos = list.getFirstVisiblePosition();
				cur_view = -1;
				list_show(gui.cur_path);
				bexplorer.setChecked(true);
				bplist.setChecked(false);
				tfilter.setVisibility(View.INVISIBLE);
				break;

			case Playlist:
				cur_view = 0;
				bexplorer.setChecked(false);
				bplist.setChecked(true);
				if (gui.filter_show)
					tfilter.setVisibility(View.VISIBLE);
				plist_show2();
				list.setSelection(gui.list_pos);
				break;
		}
	}

	void file_del(int pos, String fn) {
		queue.remove(pos);
		if (core.file_rename(fn, fn + ".deleted"))
			gui.msg_show(this, "Renamed 1 file");
		plist_show2();
		list.setSelection(gui.list_pos);
	}

	/**
	 * UI event on listview click
	 * If the view is Playlist: start playing the track at this position.
	 * If the view is Explorer:
	 * if the entry is a directory: show its contents;
	 * or create a playlist with all the files in the current directory
	 * and start playing the selected track
	 */
	void list_click(int pos) {
		if (cur_view == 0) {
			play(pos);
			return;
		}

		if (pos < ndirs) {
			list_show(fns[pos]);
			return;
		}

		String[] pl = new String[fns.length - ndirs];
		int n = 0;
		for (int i = ndirs; i != fns.length; i++) {
			pl[n++] = fns[i];
		}
		queue.clear_addmany(pl);
		core.dbglog(TAG, "added %d items", n);
		gui.msg_show(this, "Set %d playlist items", n);
		play(pos - ndirs);
	}

	/**
	 * Recurively add directory contents to this.tmp_fns
	 */
	void add_files_recursive(String dir) {
		File fdir = new File(dir);
		if (!fdir.isDirectory()) {
			if (core.track().supported(dir))
				tmp_fns.add(dir);
			return;
		}
		File[] files = fdir.listFiles();
		if (files == null)
			return;

		// sort file names (directories first)
		class FileCmp implements Comparator<File> {
			@Override
			public int compare(File f1, File f2) {
				if (f1.isDirectory() == f2.isDirectory())
					return f1.getName().compareToIgnoreCase(f2.getName());
				if (f1.isDirectory())
					return -1;
				return 1;
			}
		}
		Arrays.sort(files, new FileCmp());

		for (File f : files) {
			if (!f.isDirectory())
				if (core.track().supported(f.getName()))
					tmp_fns.add(f.getPath());
		}

		for (File f : files) {
			if (f.isDirectory())
				add_files_recursive(f.getPath());
		}
	}

	/**
	 * UI event on listview long click.
	 * Add files to the playlist.  Recursively add directory contents.
	 */
	boolean list_longclick(int pos) {
		if (cur_view == 0)
			return false; // no action for a playlist

		if (pos == 0 && updir)
			return false; // no action for a long click on "<UP>"

		tmp_fns = new ArrayList<>();
		add_files_recursive(fns[pos]);
		queue.addmany(tmp_fns.toArray(new String[0]));
		core.dbglog(TAG, "added %d items", tmp_fns.size());
		gui.msg_show(this, "Added %d items to playlist", tmp_fns.size());
		return true;
	}

	void explorer_showcur() {
		int pos = queue.cur();
		String[] pl = queue.list();
		if (pl.length == 0)
			return;
		String fn = pl[pos];
		if (!core.track().supported(fn))
			return;
		gui.cur_path = new File(fn).getParent();
		cmd(Cmd.Explorer);

		int i = 0;
		for (String s : fns) {
			if (s.equalsIgnoreCase(fn)) {
				list.setSelection(i);
				break;
			}
			i++;
		}
	}

	/**
	 * Show the playlist items
	 */
	void plist_show() {
		if (cur_view != 0)
			return;
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

	void plist_show2() {
		if (cur_view != 0)
			return;
		if (filter_strlen != 0)
			list_filter(tfilter.getText().toString());
		else
			plist_show();
	}

	void list_filter(String filter) {
		if (cur_view != 0)
			return;
		core.dbglog(TAG, "list_filter: %s", filter);

		if (filter_strlen != 0 && filter.length() == 0) {
			filter_strlen = 0;
			plist_show();
			return;
		}

		boolean inc = (filter.length() > filter_strlen);
		if (filter.length() < 2) {
			if (!inc)
				plist_show();
			return;
		}

		filter = filter.toLowerCase();

		ArrayList<Integer> fi = new ArrayList<>();
		ArrayList<String> names = new ArrayList<>();
		String[] l = queue.list();
		int n = 1;

		if (filter_strlen == 0 || !inc) {
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

		ArrayAdapter<String> adapter = new ArrayAdapter<>(this, R.layout.list_row, names.toArray(new String[0]));
		list.setAdapter(adapter);
		filtered_idx = fi;
		filter_strlen = filter.length();
	}

	/**
	 * Read directory contents and update listview
	 */
	void list_show(String path) {
		core.dbglog(TAG, "list_show: %s", path);
		ArrayList<String> fnames = new ArrayList<>();
		ArrayList<String> names = new ArrayList<>();
		boolean updir = false;
		int ndirs = 0;
		try {
			File fdir = new File(path);
			File[] files = fdir.listFiles();

			if (!path.equalsIgnoreCase(root_path)) {
				String parent = fdir.getParent();
				if (parent != null) {
					fnames.add(parent);
					names.add("<UP> - " + parent);
					updir = true;
					ndirs++;
				}
			}

			if (files != null) {
				// sort file names (directories first)
				class FileCmp implements Comparator<File> {
					@Override
					public int compare(File f1, File f2) {
						if (f1.isDirectory() == f2.isDirectory())
							return f1.getName().compareToIgnoreCase(f2.getName());
						if (f1.isDirectory())
							return -1;
						return 1;
					}
				}
				Arrays.sort(files, new FileCmp());

				for (File f : files) {
					String s;
					if (f.isDirectory()) {
						s = "<DIR> ";
						s += f.getName();
						names.add(s);
						fnames.add(f.getPath());
						ndirs++;
						continue;
					}

					if (!core.track().supported(f.getName()))
						continue;
					s = f.getName();
					names.add(s);
					fnames.add(f.getPath());
				}
			}
		} catch (Exception e) {
			core.errlog(TAG, "list_show: %s", e);
			fns = new String[0];
			return;
		}

		fns = fnames.toArray(new String[0]);
		gui.cur_path = path;
		ArrayAdapter<String> adapter = new ArrayAdapter<>(this, R.layout.list_row, names.toArray(new String[0]));
		list.setAdapter(adapter);
		this.updir = updir;
		this.ndirs = ndirs;
		core.dbglog(TAG, "added %d files", fns.length - 1);
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
	void seek(int pos) {
		trackctl.seek(pos);
	}

	void state(int st) {
		switch (st) {
			case 0:
				getSupportActionBar().setTitle("fmedia");
				bplay.setImageResource(R.drawable.ic_play);
				break;

			case 1:
				getSupportActionBar().setTitle("fmedia");
				bplay.setImageResource(R.drawable.ic_pause);
				break;

			case 2:
				getSupportActionBar().setTitle("[Paused] fmedia");
				bplay.setImageResource(R.drawable.ic_play);
				break;
		}
	}

	/**
	 * Called by Track when a new track is initialized
	 */
	int new_track(TrackHandle t) {
		lbl_name.setText(t.name);
		progs.setProgress(0);
		if (t.state == Track.State.PAUSED) {
			state(2);
		} else {
			state(1);
		}
		return 0;
	}

	/**
	 * Called by Track after a track is finished
	 */
	void close_track(TrackHandle t) {
		lbl_name.setText("");
		lbl_pos.setText("");
		progs.setProgress(0);
		state(0);
	}

	/**
	 * Called by Track during playback
	 */
	int update_track(TrackHandle t) {
		if (t.state == Track.State.PAUSED) {
			state(2);
		}
		int pos = t.pos / 1000;
		int dur = t.time_total / 1000;
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
