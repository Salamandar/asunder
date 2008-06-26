/*
Asunder

Copyright(C) 2005 Eric Lathrop <eric@ericlathrop.com>
Copyright(C) 2007 Andrew Smith <http://littlesvr.ca/misc/contactandrew.php>

Any code in this file may be redistributed or modified under the terms of
the GNU General Public Licence as published by the Free Software 
Foundation; version 2 of the licence.

*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>

#include "threads.h"
#include "main.h"
#include "prefs.h"
#include "util.h"
#include "wrappers.h"
#include "support.h"
#include "interface.h"

static GMutex * barrier = NULL;
static GCond * available = NULL;
static int counter;

static FILE * playlist_wav = NULL;
static FILE * playlist_mp3 = NULL;
static FILE * playlist_ogg = NULL;
static FILE * playlist_flac = NULL;
static FILE * playlist_wavpack = NULL;

/* ripping or encoding, so that can know not to clear the tracklist on eject */
bool working;
/* for canceling */
bool aborted;
/* for stopping the tracking thread */
bool allDone;

static GThread * ripper;
static GThread * encoder;
static GThread * tracker;

static int tracks_to_rip;
static double rip_percent;
static double mp3_percent;
static double ogg_percent;
static double flac_percent;
static double wavpack_percent;
static int rip_tracks_completed;
static int encode_tracks_completed;

extern bool overwriteAll;
extern bool overwriteNone;

// aborts ripping- stops all the threads and return to normal execution
void abort_threads()
{
    aborted = true;

    if (cdparanoia_pid != 0) 
        kill(cdparanoia_pid, SIGKILL);
    if (lame_pid != 0) 
        kill(lame_pid, SIGKILL);
    if (oggenc_pid != 0) 
        kill(oggenc_pid, SIGKILL);
    if (flac_pid != 0) 
        kill(flac_pid, SIGKILL);
    
    /* wait until all the worker threads are done */
    while (cdparanoia_pid != 0 || lame_pid != 0 || oggenc_pid != 0 || flac_pid != 0)
    {
        debugLog("w1");
        usleep(100000);
    }
    
    g_cond_signal(available);
    
    debugLog("Aborting: 1\n");
    g_thread_join(ripper);
    debugLog("Aborting: 2\n");
    g_thread_join(encoder);
    debugLog("Aborting: 3\n");
    g_thread_join(tracker);
    debugLog("Aborting: 4 (All threads joined)\n");
    
    gtk_widget_hide(win_ripping);
    gdk_flush();
    working = false;
    show_completed_dialog(numCdparanoiaOk + numLameOk + numOggOk + numFlacOk,
                          numCdparanoiaFailed + numLameFailed + numOggFailed + numFlacFailed);
}

// spawn needed threads and begin ripping
void dorip()
{
    working = true;
    aborted = false;
    allDone = false;
    counter = 0;
    barrier = g_mutex_new();
    available = g_cond_new();
    rip_percent = 0.0;
    mp3_percent = 0.0;
    ogg_percent = 0.0;
    flac_percent = 0.0;
    wavpack_percent = 0.0;
    rip_tracks_completed = 0;
    encode_tracks_completed = 0;
    
    const char * albumartist = gtk_entry_get_text(GTK_ENTRY(lookup_widget(win_main, "album_artist")));
    const char * albumtitle = gtk_entry_get_text(GTK_ENTRY(lookup_widget(win_main, "album_title")));
    char * albumdir = parse_format(global_prefs->format_albumdir, 0, albumartist, albumtitle, NULL);
    char * playlist = parse_format(global_prefs->format_playlist, 0, albumartist, albumtitle, NULL);
    
    overwriteAll = false;
    overwriteNone = false;
    
    // make sure there's at least one format to rip to
    if (!global_prefs->rip_wav && !global_prefs->rip_mp3 && !global_prefs->rip_ogg && 
        !global_prefs->rip_flac && !global_prefs->rip_wavpack)
    {
        GtkWidget * dialog;
        dialog = gtk_message_dialog_new(GTK_WINDOW(win_main), 
                                        GTK_DIALOG_DESTROY_WITH_PARENT, 
                                        GTK_MESSAGE_ERROR, 
                                        GTK_BUTTONS_OK, 
                                        _("No ripping/encoding method selected. Please enable one from the "
                                        "'Preferences' menu."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        free(albumdir);
        free(playlist);
        return;
    }
    
    // make sure there's some tracks to rip
    GtkListStore * store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(lookup_widget(win_main, "tracklist"))));
    GtkTreeIter iter;
    gboolean rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    tracks_to_rip = 0;
    int riptrack;
    while(rowsleft)
    {
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                           COL_RIPTRACK, &riptrack,
                           -1);
        if (riptrack) 
            tracks_to_rip++;
        rowsleft = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
    }
    if (tracks_to_rip == 0)
    {
        GtkWidget * dialog;
        dialog = gtk_message_dialog_new(GTK_WINDOW(win_main), 
                                        GTK_DIALOG_DESTROY_WITH_PARENT, 
                                        GTK_MESSAGE_ERROR, 
                                        GTK_BUTTONS_OK, 
                                        _("No tracks were selected for ripping/encoding. "
                                        "Please select at least one track."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        free(albumdir);
        free(playlist);
        return;
    }
    
    /* CREATE the album directory */
    char * dirpath = make_filename(prefs_get_music_dir(global_prefs), albumdir, NULL, NULL);
    debugLog("Making album directory '%s'\n", dirpath);
    
    if ( recursive_mkdir(dirpath, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) != 0 && 
         errno != EEXIST )
    {
        GtkWidget * dialog;
        dialog = gtk_message_dialog_new(GTK_WINDOW(win_main), 
                                        GTK_DIALOG_DESTROY_WITH_PARENT, 
                                        GTK_MESSAGE_ERROR, 
                                        GTK_BUTTONS_OK, 
                                        "Unable to create directory '%s': %s", 
                                        dirpath, strerror(errno));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        free(dirpath);
        free(albumdir);
        free(playlist);
        return;
    }
    
    free(dirpath);
    /* END CREATE the album directory */
    
    if (global_prefs->make_playlist)
    {
        debugLog("Creating playlists\n");
        
        if (global_prefs->rip_wav)
        {
            char * filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "wav.m3u");
            
            make_playlist(filename, &playlist_wav);
            
            free(filename);
        }
        if (global_prefs->rip_mp3)
        {
            char * filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "mp3.m3u");
            
            make_playlist(filename, &playlist_mp3);
            
            free(filename);
        }
        if (global_prefs->rip_ogg)
        {
            char * filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "ogg.m3u");
            
            make_playlist(filename, &playlist_ogg);
            
            free(filename);
        }
        if (global_prefs->rip_flac)
        {
            char * filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "flac.m3u");
            
            make_playlist(filename, &playlist_flac);
            
            free(filename);
        }
        if (global_prefs->rip_wavpack)
        {
            char * filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "wv.m3u");
            
            make_playlist(filename, &playlist_wavpack);
            
            free(filename);
        }
    }
    
    free(albumdir);
    free(playlist);
    
    gtk_widget_show(win_ripping);
    
    numCdparanoiaFailed = 0;
    numLameFailed = 0;
    numOggFailed = 0;
    numFlacFailed = 0;
    numWavpackFailed = 0;
    
    numCdparanoiaOk = 0;
    numLameOk = 0;
    numOggOk = 0;
    numFlacOk = 0;
    numWavpackOk = 0;
    
    ripper = g_thread_create(rip, NULL, TRUE, NULL);
    encoder = g_thread_create(encode, NULL, TRUE, NULL);
    tracker = g_thread_create(track, NULL, TRUE, NULL);
}

// the thread that handles ripping tracks to WAV files
gpointer rip(gpointer data)
{
    GtkTreeIter iter;

    int riptrack;
    int tracknum;
    const char * trackartist;
    const char * tracktitle;
    
    char * albumdir = NULL;
    char * musicfilename = NULL;
    char * wavfilename = NULL;

    gdk_threads_enter();
        GtkListStore * store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(lookup_widget(win_main, "tracklist"))));
        gboolean single_artist = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lookup_widget(win_main, "single_artist")));
        const char * albumartist = gtk_entry_get_text(GTK_ENTRY(lookup_widget(win_main, "album_artist")));
        const char * albumtitle = gtk_entry_get_text(GTK_ENTRY(lookup_widget(win_main, "album_title")));

        gboolean rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    gdk_threads_leave();
    while(rowsleft)
    {
        gdk_threads_enter();
        gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
            COL_RIPTRACK, &riptrack,
            COL_TRACKNUM, &tracknum,
            COL_TRACKARTIST, &trackartist,
            COL_TRACKTITLE, &tracktitle,
            -1);
        gdk_threads_leave();
        
        if (single_artist)
        {
            trackartist = albumartist;
        }
        
        if (riptrack)
        {
            albumdir = parse_format(global_prefs->format_albumdir, 0, albumartist, albumtitle, NULL);
            musicfilename = parse_format(global_prefs->format_music, tracknum, trackartist, albumtitle, tracktitle);
            wavfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "wav");
            
            debugLog("Ripping track %d to \"%s\"\n", tracknum, wavfilename);
            
            if (aborted) g_thread_exit(NULL);
            
            struct stat statStruct;
            int rc;
            bool doRip;
            
            rc = stat(wavfilename, &statStruct);
            if(rc == 0)
            {
                gdk_threads_enter();
                    if(confirmOverwrite(wavfilename))
                        doRip = true;
                    else
                        doRip = false;
                gdk_threads_leave();
            }
            else
                doRip = true;
            
            if(doRip)
                cdparanoia(global_prefs->cdrom, tracknum, wavfilename, &rip_percent);

            free(albumdir);
            free(musicfilename);
            free(wavfilename);
            
            rip_percent = 0.0;
            rip_tracks_completed++;
        }

        g_mutex_lock(barrier);
        counter++;
        g_mutex_unlock(barrier);
        g_cond_signal(available);
        
        if (aborted)
            g_thread_exit(NULL);
        
        gdk_threads_enter();
            rowsleft = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
        gdk_threads_leave();
    }
    
    // no more tracks to rip, safe to eject
    if (global_prefs->eject_on_done)
    {
        eject_disc(global_prefs->cdrom);
    }
    
    return NULL;
}

// the thread that handles encoding WAV files to all other formats
gpointer encode(gpointer data)
{
    GtkTreeIter iter;

    int riptrack;
    int tracknum;
    char * trackartist = NULL;
    char * tracktitle = NULL;
    char * tracktime = NULL;
    char * genre = NULL;
    unsigned year;
    char yearStr[5];
    char * yearStrPtr;
    int min;
    int sec;
    int i;
    int rc;
    
    char * album_artist = NULL;
    char * album_title = NULL;
    
    char * albumdir = NULL;
    char * musicfilename = NULL;
    char * wavfilename = NULL;
    char * mp3filename = NULL;
    char * oggfilename = NULL;
    char * flacfilename = NULL;
    char * wavpackfilename = NULL;
    char * wavpackfilename2 = NULL;
    struct stat statStruct;
    bool doEncode;
    
    gdk_threads_enter();
        GtkListStore * store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(lookup_widget(win_main, "tracklist"))));
        gboolean single_artist = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lookup_widget(win_main, "single_artist")));
        
        const char * temp_album_artist = gtk_entry_get_text(GTK_ENTRY(lookup_widget(win_main, "album_artist")));
        album_artist = malloc(sizeof(char) * (strlen(temp_album_artist)+1));
        if (album_artist == NULL)
            fatalError("malloc(sizeof(char) * (strlen(temp_album_artist)+1)) failed. Out of memory.");
        strncpy(album_artist, temp_album_artist, strlen(temp_album_artist)+1);
        
        const char * temp_album_title = gtk_entry_get_text(GTK_ENTRY(lookup_widget(win_main, "album_title")));
        album_title = malloc(sizeof(char) * (strlen(temp_album_title)+1));
        if (album_title == NULL)
            fatalError("malloc(sizeof(char) * (strlen(temp_album_artist)+1)) failed. Out of memory.");
        strncpy(album_title, temp_album_title, strlen(temp_album_title)+1);

        gboolean rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
    gdk_threads_leave();
    while(rowsleft)
    {
        g_mutex_lock(barrier);
        while ((counter < 1) && (!aborted))
        {
            g_cond_wait(available, barrier);
        }
        counter--;
        g_mutex_unlock(barrier);
        if (aborted) g_thread_exit(NULL);
        
        gdk_threads_enter();
            gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
                COL_RIPTRACK, &riptrack,
                COL_TRACKNUM, &tracknum,
                COL_TRACKARTIST, &trackartist,
                COL_TRACKTITLE, &tracktitle,
                COL_TRACKTIME, &tracktime,
                COL_GENRE, &genre,
                COL_YEAR, &year,
                -1);
        gdk_threads_leave();
        sscanf(tracktime, "%d:%d", &min, &sec);
        
        snprintf(yearStr, 5, "%d", year);
        if(year == 0)
            yearStrPtr = NULL;
        else
            yearStrPtr = yearStr;
        
        if (single_artist)
        {
            trackartist = album_artist;
        }
        
        if (riptrack)
        {
            albumdir = parse_format(global_prefs->format_albumdir, 0, album_artist, album_title, NULL);
            musicfilename = parse_format(global_prefs->format_music, tracknum, trackartist, album_title, tracktitle);
            wavfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "wav");
            mp3filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "mp3");
            oggfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "ogg");
            flacfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "flac");
            wavpackfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "wv");
            wavpackfilename2 = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "wvc");
            
            if (global_prefs->rip_mp3)
            {
                debugLog("Encoding track %d to \"%s\"\n", tracknum, mp3filename);
                
                if (aborted) g_thread_exit(NULL);
                
                rc = stat(mp3filename, &statStruct);
                if(rc == 0)
                {
                    gdk_threads_enter();
                        if(confirmOverwrite(mp3filename))
                            doEncode = true;
                        else
                            doEncode = false;
                    gdk_threads_leave();
                }
                else
                    doEncode = true;
                
                if(doEncode)
                    lame(tracknum, trackartist, album_title, tracktitle, genre, yearStrPtr, wavfilename, mp3filename, 
                         global_prefs->mp3_vbr, global_prefs->mp3_bitrate, &mp3_percent);
                
                if (aborted) g_thread_exit(NULL);

                if (playlist_mp3)
                {
                    fprintf(playlist_mp3, "#EXTINF:%d,%s - %s\n", (min*60)+sec, trackartist, tracktitle);
                    fprintf(playlist_mp3, "%s\n", basename(mp3filename));
                    fflush(playlist_mp3);
                }
            }
            if (global_prefs->rip_ogg)
            {
                debugLog("Encoding track %d to \"%s\"\n", tracknum, oggfilename);
                
                if (aborted) g_thread_exit(NULL);
                
                rc = stat(oggfilename, &statStruct);
                if(rc == 0)
                {
                    gdk_threads_enter();
                        if(confirmOverwrite(oggfilename))
                            doEncode = true;
                        else
                            doEncode = false;
                    gdk_threads_leave();
                }
                else
                    doEncode = true;
                
                if(doEncode)
                    oggenc(tracknum, trackartist, album_title, tracktitle, genre, wavfilename, 
                           oggfilename, global_prefs->ogg_quality, &ogg_percent);
                
                if (aborted) g_thread_exit(NULL);

                if (playlist_ogg)
                {
                    fprintf(playlist_ogg, "#EXTINF:%d,%s - %s\n", (min*60)+sec, trackartist, tracktitle);
                    fprintf(playlist_ogg, "%s\n", basename(oggfilename));
                    fflush(playlist_ogg);
                }
            }
            if (global_prefs->rip_flac)
            {
                debugLog("Encoding track %d to \"%s\"\n", tracknum, flacfilename);
                
                if (aborted) g_thread_exit(NULL);
                
                rc = stat(flacfilename, &statStruct);
                if(rc == 0)
                {
                    gdk_threads_enter();
                        if(confirmOverwrite(flacfilename))
                            doEncode = true;
                        else
                            doEncode = false;
                    gdk_threads_leave();
                }
                else
                    doEncode = true;
                
                if(doEncode)
                    flac(tracknum, trackartist, album_title, tracktitle, genre, yearStrPtr, wavfilename, 
                         flacfilename, global_prefs->flac_compression, &flac_percent);
                
                if (aborted) g_thread_exit(NULL);
                
                if (playlist_flac)
                {
                    for (i=strlen(flacfilename); ((i>0) && (flacfilename[i] != '/')); i--);
                    fprintf(playlist_flac, "#EXTINF:%d,%s - %s\n", (min*60)+sec, trackartist, tracktitle);
                    fprintf(playlist_flac, "%s\n", basename(flacfilename));
                    fflush(playlist_flac);
                }
            }
            if (global_prefs->rip_wavpack)
            {
                debugLog("Encoding track %d to \"%s\"\n", tracknum, wavpackfilename);
                
                if (aborted) g_thread_exit(NULL);
                
                rc = stat(wavpackfilename, &statStruct);
                int rc2 = stat(wavpackfilename2, &statStruct);
                if(rc == 0 || rc2 == 0)
                {
                    gdk_threads_enter();
                        if(confirmOverwrite(wavpackfilename))
                            doEncode = true;
                        else
                            doEncode = false;
                    gdk_threads_leave();
                }
                else
                    doEncode = true;
                
                if(doEncode)
                {
                    wavpack(tracknum, wavfilename, 
                            global_prefs->wavpack_compression, global_prefs->wavpack_hybrid, 
                            int_to_wavpack_bitrate(global_prefs->wavpack_bitrate), &wavpack_percent);
                }
                
                if (aborted) g_thread_exit(NULL);
                
                if (playlist_wavpack)
                {
                    for (i=strlen(flacfilename); ((i>0) && (wavpackfilename[i] != '/')); i--);
                    fprintf(playlist_wavpack, "#EXTINF:%d,%s - %s\n", (min*60)+sec, trackartist, tracktitle);
                    fprintf(playlist_wavpack, "%s\n", basename(wavpackfilename));
                    fflush(playlist_wavpack);
                }
            }
            if (!global_prefs->rip_wav)
            {
                debugLog("Removing track %d WAV file\n", tracknum);
                
                if (unlink(wavfilename) != 0)
                {
                    printf("Unable to delete WAV file \"%s\": %s\n", wavfilename, strerror(errno));
                }
            } else {
                if (playlist_wav)
                {
                    for (i=strlen(wavfilename); ((i>0) && (wavfilename[i] != '/')); i--);
                    fprintf(playlist_wav, "#EXTINF:%d,%s - %s\n", (min*60)+sec, trackartist, tracktitle);
                    fprintf(playlist_wav, "%s\n", basename(wavfilename));
                    fflush(playlist_wav);
                }
            }
            
            free(albumdir);
            free(musicfilename);
            free(wavfilename);
            free(mp3filename);
            free(oggfilename);
            free(flacfilename);
            free(wavpackfilename);
            
            mp3_percent = 0.0;
            ogg_percent = 0.0;
            flac_percent = 0.0;
            wavpack_percent = 0.0;
            encode_tracks_completed++;
        }
        
        if (aborted) g_thread_exit(NULL);
        
        gdk_threads_enter();
            rowsleft = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
        gdk_threads_leave();
    }
    
    free(album_artist);
    free(album_title);
    
    if (playlist_wav) fclose(playlist_wav);
    playlist_wav = NULL;
    if (playlist_mp3) fclose(playlist_mp3);
    playlist_mp3 = NULL;
    if (playlist_ogg) fclose(playlist_ogg);
    playlist_ogg = NULL;
    if (playlist_flac) fclose(playlist_flac);
    playlist_flac = NULL;
    
    g_mutex_free(barrier);
    barrier = NULL;
    g_cond_free(available);
    available = NULL;
    
    /* wait until all the worker threads are done */
    while (cdparanoia_pid != 0 || lame_pid != 0 || oggenc_pid != 0 || flac_pid != 0 || wavpack_pid != 0)
    {
        debugLog("w2");
        usleep(100000);
    }
    
    allDone = true; // so the tracker thread will exit
    working = false;
    
    gdk_threads_enter();
        gtk_widget_hide(win_ripping);
        gdk_flush();
        show_completed_dialog(numCdparanoiaOk + numLameOk + numOggOk + numFlacOk + numWavpackOk,
                              numCdparanoiaFailed + numLameFailed + numOggFailed + numFlacFailed + numWavpackFailed);
    gdk_threads_leave();
    
    return NULL;
}

// the thread that calculates the progress of the other threads and updates the progress bars
gpointer track(gpointer data)
{
    int parts = 1;
    if(global_prefs->rip_mp3) 
        parts++;
    if(global_prefs->rip_ogg) 
        parts++;
    if(global_prefs->rip_flac) 
        parts++;
    if(global_prefs->rip_wavpack) 
        parts++;
    
    gdk_threads_enter();
        GtkProgressBar * progress_total = GTK_PROGRESS_BAR(lookup_widget(win_ripping, "progress_total"));
        GtkProgressBar * progress_rip = GTK_PROGRESS_BAR(lookup_widget(win_ripping, "progress_rip"));
        GtkProgressBar * progress_encode = GTK_PROGRESS_BAR(lookup_widget(win_ripping, "progress_encode"));
        
        gtk_progress_bar_set_fraction(progress_total, 0.0);
        gtk_progress_bar_set_text(progress_total, _("Waiting..."));
        gtk_progress_bar_set_fraction(progress_rip, 0.0);
        gtk_progress_bar_set_text(progress_rip, _("Waiting..."));
        if (parts > 1)
        {
            gtk_progress_bar_set_fraction(progress_encode, 0.0);
            gtk_progress_bar_set_text(progress_encode, _("Waiting..."));
        } else {
            gtk_progress_bar_set_fraction(progress_encode, 1.0);
            gtk_progress_bar_set_text(progress_encode, "100% (0/0)");
        }
    gdk_threads_leave();
    
    double prip;
    char srip[13];
    double pencode;
    char sencode[13];
    double ptotal;
    char stotal[5];
    char windowTitle[15]; /* "Asunder - 100%" */

    while (!aborted && !allDone)
    {
        debugLog("completed tracks %d, rip %.2lf%%; encoded tracks %d, "
                 "mp3 %.2lf%% ogg %.2lf%% flac %.2lf%% wavpack %.2lf%%\n", 
                 rip_tracks_completed, rip_percent, encode_tracks_completed, 
                 mp3_percent, ogg_percent, flac_percent, wavpack_percent);
        
        prip = (rip_tracks_completed+rip_percent) / tracks_to_rip;
        snprintf(srip, 13, "%d%% (%d/%d)", (int)(prip*100), rip_tracks_completed, tracks_to_rip);
        if (parts > 1)
        {
            pencode = ((double)encode_tracks_completed/(double)tracks_to_rip) + ((mp3_percent+ogg_percent+flac_percent+wavpack_percent)/(parts-1)/tracks_to_rip);
            snprintf(sencode, 13, "%d%% (%d/%d)", (int)(pencode*100), encode_tracks_completed, tracks_to_rip);
            ptotal = prip/parts + pencode*(parts-1)/parts;
        } else {
            ptotal = prip;
        }
        snprintf(stotal, 5, "%d%%", (int)(ptotal*100));
        
        strcpy(windowTitle, "Asunder - ");
        strcat(windowTitle, stotal);
        
        gdk_threads_enter();
            gtk_progress_bar_set_fraction(progress_rip, prip);
            gtk_progress_bar_set_text(progress_rip, srip);
            if (parts > 1)
            {
                gtk_progress_bar_set_fraction(progress_encode, pencode);
                gtk_progress_bar_set_text(progress_encode, sencode);
            }
            
            gtk_progress_bar_set_fraction(progress_total, ptotal);
            gtk_progress_bar_set_text(progress_total, stotal);
            
            gtk_window_set_title(GTK_WINDOW(win_main), windowTitle);
        gdk_threads_leave();
        
        usleep(100000);
    }
    
    gdk_threads_enter();
        gtk_window_set_title(GTK_WINDOW(win_main), "Asunder");
    gdk_threads_leave();
    
    return NULL;
}

