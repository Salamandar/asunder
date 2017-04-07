/*
Asunder

Copyright(C) 2005 Eric Lathrop <eric@ericlathrop.com>
Copyright(C) 2007 Andrew Smith <http://littlesvr.ca/contact.php>

Any code in this file may be redistributed or modified under the terms of
the GNU General Public Licence as published by the Free Software 
Foundation; version 2 of the licence.

*/

#define _GNU_SOURCE

#include <sys/types.h>
#include <unistd.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>

#include "main.h"
#include "wrappers.h"
#include "threads.h"
#include "prefs.h"
#include "util.h"

pid_t cdparanoia_pid;
pid_t lame_pid;
pid_t oggenc_pid;
pid_t opusenc_pid;
pid_t flac_pid;
pid_t wavpack_pid;
pid_t monkey_pid;
pid_t musepack_pid;
pid_t aac_pid;

int numCdparanoiaFailed;
int numLameFailed;
int numOggFailed;
int numOpusFailed;
int numFlacFailed;
int numWavpackFailed;
int numMonkeyFailed;
int numMusepackFailed;
int numAacFailed;

int numCdparanoiaOk;
int numLameOk;
int numOggOk;
int numOpusOk;
int numFlacOk;
int numWavpackOk;
int numMonkeyOk;
int numMusepackOk;
int numAacOk;

int numchildren = 0;
static bool waitBeforeSigchld;

void blockSigChld(void)
{
    sigset_t block_chld;
    
    sigemptyset(&block_chld);
    sigaddset(&block_chld, SIGCHLD);
    
    sigprocmask(SIG_BLOCK, &block_chld, NULL);
    
    /*!! for some reason the blocking above doesn't work, so do this for now */
    waitBeforeSigchld = true;
}

void unblockSigChld(void)
{
    sigset_t block_chld;
    
    sigemptyset(&block_chld);
    sigaddset(&block_chld, SIGCHLD);
    
    sigprocmask(SIG_UNBLOCK, &block_chld, NULL);
    
    waitBeforeSigchld = false;
}

extern pid_t cdparanoia_pid;
extern pid_t lame_pid;
extern pid_t oggenc_pid;
extern pid_t opusenc_pid;
extern pid_t flac_pid;
extern pid_t wavpack_pid;
extern pid_t monkey_pid;
extern pid_t musepack_pid;
extern pid_t aac_pid;

// Signal handler to find out when our child has exited.
// Do not pot any printf or syslog into here, it causes a deadlock.
//
void sigchld(int signum)
{
    int status;
    pid_t pid;
    
    pid = wait(&status);
    
    /* this is because i can't seem to be able to block sigchld: */
    while(waitBeforeSigchld)
        usleep(100);
    
    if (status != 0)
    {
        if (pid == cdparanoia_pid)
        {
            cdparanoia_pid = 0;
            if(global_prefs->rip_wav)
                numCdparanoiaFailed++;
        }
        else if (pid == lame_pid)
        {
            lame_pid = 0;
            numLameFailed++;
        }
        else if (pid == oggenc_pid)
        {
            oggenc_pid = 0;
            numOggFailed++;
        }
        else if (pid == opusenc_pid)
        {
            opusenc_pid = 0;
            numOpusFailed++;
        }
        else if (pid == flac_pid)
        {
            flac_pid = 0;
            numFlacFailed++;
        }
        else if (pid == wavpack_pid)
        {
            wavpack_pid = 0;
            numWavpackFailed++;
        }
        else if (pid == monkey_pid)
        {
            monkey_pid = 0;
            numMonkeyFailed++;
        }
        else if (pid == musepack_pid)
        {
            musepack_pid = 0;
            numMusepackFailed++;
        }
        else if (pid == aac_pid)
        {
            aac_pid = 0;
            numAacFailed++;
        }
    }
    else
    {
        if (pid == cdparanoia_pid)
        {
            cdparanoia_pid = 0;
            if(global_prefs->rip_wav)
                numCdparanoiaOk++;
        }
        else if (pid == lame_pid)
        {
            lame_pid = 0;
            numLameOk++;
        }
        else if (pid == oggenc_pid)
        {
            oggenc_pid = 0;
            numOggOk++;
        }
        else if (pid == opusenc_pid)
        {
            opusenc_pid = 0;
            numOpusOk++;
        }
        else if (pid == flac_pid)
        {
            flac_pid = 0;
            numFlacOk++;
        }
        else if (pid == wavpack_pid)
        {
            wavpack_pid = 0;
            numWavpackOk++;
        }
        else if (pid == monkey_pid)
        {
            monkey_pid = 0;
            numMonkeyOk++;
        }
        else if (pid == musepack_pid)
        {
            musepack_pid = 0;
            numMusepackOk++;
        }
        else if (pid == aac_pid)
        {
            aac_pid = 0;
            numAacOk++;
        }
    }
}

// fork() and exec() the file listed in "args"
//
// args - a valid array for execvp()
// toread - the file descriptor to pipe back to the parent
// p - a place to write the PID of the exec'ed process
// dir - directory to run program in
// 
// returns - a file descriptor that reads whatever the program outputs on "toread"
int exec_with_output(const char * args[], int toread, pid_t * p, const char * dir)
{
    char logStr[1024];
    int pipefd[2];
    
    blockSigChld();
    
    if (pipe(pipefd) != 0)
        fatalError("exec_with_output(): failed to create a pipe");
    
    if ((*p = fork()) == 0)
    {
        // im the child
        // i get to execute the command
        
        // close the side of the pipe we don't need
        close(pipefd[0]);
        
        /* this causes a segfault on fedora, and I don't really see why it's needed anyway */
        //~ // close all standard streams to keep output clean
        //~ close(STDOUT_FILENO);
        //~ close(STDIN_FILENO);
        //~ close(STDERR_FILENO);
        
        /* instead redirect to /dev/null */
        if (gbl_null_fd != -1)
        {
            dup2(STDOUT_FILENO, gbl_null_fd);
            close(STDOUT_FILENO);
            dup2(STDERR_FILENO, gbl_null_fd);
            close(STDERR_FILENO);
        }
        
        // setup output
        dup2(pipefd[1], toread);
        close(pipefd[1]);
        
        // Change directory if required
        if (dir && (chdir(dir) < 0))
        {
            snprintf(logStr, 1024, "Failed to change directory for %s, errno=%d", args[0], errno);
            debugLog(logStr);
            exit(0);
        }
        
        // call execvp
        execvp(args[0], (char **)args);
        
        // should never get here
        fatalError("exec_with_output(): execvp() failed");
    }
    
    int count;
    snprintf(logStr, 1024, "%d started: %s ", *p, args[0]);
    for (count = 1; args[count] != NULL; count++)
    {
        if (strlen(logStr) + 1 + strlen(args[count]) < 1024)
        {
            strcat(logStr, " ");
            strcat(logStr, args[count]);
        }
    }
    debugLog(logStr);
    
    // i'm the parent, get ready to wait for children
    numchildren++;
    
    // close the side of the pipe we don't need
    close(pipefd[1]);
    
    unblockSigChld();
    
    return pipefd[0];
}

// uses cdparanoia to rip a WAV track from a cdrom
//
// cdrom    - the path to the cdrom device
// tracknum - the track to rip
// filename - the name of the output WAV file
// progress - the percent done
void cdparanoia(char * cdrom, int tracknum, char * filename, double * progress)
{
    const char * args[8];
    int pos;
    char logStr[1024];
    int fd;
    char buf[256];
    int size;
    
    int start;
    int end;
    int code;
    char type[200];
    int sector;
    
    char trackstring[4];
    
    snprintf(trackstring, 4, "%d", tracknum);

    // Evade cdparanoia's 245 char path limit by changing to the target
    // directory (in exec_with_output()) and using a temporary file name
    // with no absolute path.
    char trackname[16];
    snprintf(trackname, sizeof(trackname), "x%02d.wav", tracknum);
    gchar * dir = g_path_get_dirname(filename);
    gchar * xfilename = g_build_filename(dir, trackname, NULL);
    
    pos = 0;
    args[pos++] = "cdparanoia";
    if (global_prefs->do_fast_rip)
        args[pos++] = "-Z";
    args[pos++] = "-e";
    args[pos++] = "-d";
    args[pos++] = cdrom;
    args[pos++] = trackstring;
    args[pos++] = trackname;
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &cdparanoia_pid, dir);
    
    // to convert the progress number stat cdparanoia spits out
    // into sector numbers divide by 1176
    // note: only use the "[wrote]" numbers
    do
    {
        pos = -1;
        bool interrupted;
        do
        {
            interrupted = FALSE;
            
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                debugLog("cdparanoia() interrupted");
                interrupted = TRUE;
            }
        } while ((size > 0 && pos < 255 && buf[pos] != '\n') || interrupted);
        buf[pos] = '\0';

        if ((buf[0] == 'R') && (buf[1] == 'i'))
        {
            sscanf(buf, "Ripping from sector %d", &start);
        } else if (buf[0] == '\t') {
            sscanf(buf, "\t to sector %d", &end);
        } else if (buf[0] == '#') {
            sscanf(buf, "##: %d %s @ %d", &code, type, &sector);
            sector /= 1176;
            if (strncmp("[wrote]", type, 7) == 0)
            {
                *progress = (double)(sector-start)/(end-start);
            }
        }
    } while (size > 0);
    
    snprintf(logStr, 1024, "Ripping %s finished\n", trackstring);
    debugLog(logStr);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (cdparanoia_pid != 0)
    {
        debugLog("w3\n");
        usleep(100000);
    }

    rename(xfilename, filename);
    g_free(xfilename);
    g_free(dir);

    *progress = 1;
}

// uses LAME to encode a WAV file into a MP3 and tag it
//
// tracknum - the track number
// artist - the artist's name
// album - the album the song came from
// title - the name of the song
// wavfilename - the path to the WAV file to encode
// mp3filename - the path to the output MP3 file
// vbr - (boolean) wether or not to encode with Variable Bit Rate
// bitrate - the bitrate to encode at (or maximum bitrate if using VBR)
// progress - the percent done
void lame(int tracknum,
          char * artist,
          char * album,
          char * title,
          char * genre,
          char * year,
          char * wavfilename,
          char * mp3filename,
          int vbr,
          int bitrate,
          double * progress)
{
    int fd;
    
    char buf[256];
    int size;
    int pos;
    
    int sector;
    int end;
    
    char tracknum_text[4];
    char bitrate_text[4];
    const char * args[19];

//    fprintf( stderr, " lame()   Genre: %s Artist: %s Title: %s\n", genre, artist, title );	// lnr

    snprintf(tracknum_text, 4, "%d", tracknum);
    
    pos = 0;
    args[pos++] = "lame";
    if (vbr)
    {
        args[pos++] = "-V";
        snprintf(bitrate_text, 4, "%d", int_to_vbr_int(bitrate));
    } else {
        args[pos++] = "-b";
        snprintf(bitrate_text, 4, "%d", int_to_bitrate(bitrate, vbr));
    }
    args[pos++] = bitrate_text;
    args[pos++] = "--id3v2-only";
    if ((tracknum > 0) && (tracknum < 100))
    {
        args[pos++] = "--tn";
        args[pos++] = tracknum_text;
    }
    if ((artist != NULL) && (strlen(artist) > 0))
    {
        args[pos++] = "--ta";
        args[pos++] = artist;
    }
    if ((album != NULL) && (strlen(album) > 0))
    {
        args[pos++] = "--tl";
        args[pos++] = album;
    }
    if ((title != NULL) && (strlen(title) > 0))
    {
        args[pos++] = "--tt";
        args[pos++] = title;
    }

    // lame refuses to accept some genres that come from cddb, and users get upset
    // No longer an issue - users can now edit the genre field -lnr 

    // if (false && (genre != NULL) && (strlen(genre) > 0))
    if (( genre != NULL )								// lnr
    &&  ( strlen( genre )))
    {
        args[pos++] = "--tg";
        args[pos++] = genre;
    }

    if ((year != NULL) && (strlen(year) > 0))
    {
        args[pos++] = "--ty";
        args[pos++] = year;
    }
    args[pos++] = wavfilename;
    args[pos++] = mp3filename;
    args[pos++] = NULL;

    fd = exec_with_output(args, STDERR_FILENO, &lame_pid, NULL);
    
    do
    {
        pos = -1;
        bool interrupted;
        do
        {
            interrupted = FALSE;
            
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                debugLog("lame() interrupted");
                interrupted = TRUE;
            }
            
        } while ((size > 0 && pos < 255 && buf[pos] != '\r' && buf[pos] != '\n') || interrupted);
        buf[pos] = '\0';
        
        if (sscanf(buf, "%d/%d", &sector, &end) == 2)
        {
            *progress = (double)sector/end;
        }
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (lame_pid != 0)
    {
        debugLog("w4\n");
        usleep(100000);
    }
    *progress = 1;
}

// uses oggenc to encode a WAV file into a OGG and tag it
//
// tracknum - the track number
// artist - the artist's name
// album - the album the song came from
// title - the name of the song
// wavfilename - the path to the WAV file to encode
// oggfilename - the path to the output OGG file
// quality_level - how hard to compress the file (0-10)
// progress - the percent done
void oggenc(int tracknum,
            char * artist,
            char * album,
            char * title,
            char * year,
            char * genre,
            char * wavfilename,
            char * oggfilename,
            int quality_level,
            double * progress)
{
    int fd;

    char buf[256];
    int size;
    int pos;
    
    int sector;
    int end;

    char tracknum_text[4];
    char quality_level_text[3];
    const char * args[19];

    snprintf(tracknum_text, 4, "%d", tracknum);
    snprintf(quality_level_text, 3, "%d", quality_level);
    
    pos = 0;
    args[pos++] = "oggenc";
    args[pos++] = "-q";
    args[pos++] = quality_level_text;
    
    if ((tracknum > 0) && (tracknum < 100))
    {
        args[pos++] = "-N";
        args[pos++] = tracknum_text;
    }
    if ((artist != NULL) && (strlen(artist) > 0))
    {
        args[pos++] = "-a";
        args[pos++] = artist;
    }
    if ((album != NULL) && (strlen(album) > 0))
    {
        args[pos++] = "-l";
        args[pos++] = album;
    }
    if ((title != NULL) && (strlen(title) > 0))
    {
        args[pos++] = "-t";
        args[pos++] = title;
    } 
    if ((year != NULL) && (strlen(year) > 0))
    {
        args[pos++] = "-d";
        args[pos++] = year;
    }
    if ((genre != NULL) && (strlen(genre) > 0))
    {
        args[pos++] = "-G";
        args[pos++] = genre;
    }
    args[pos++] = wavfilename;
    args[pos++] = "-o";
    args[pos++] = oggfilename;
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &oggenc_pid, NULL);
    
    do
    {
        pos = -1;
        bool interrupted;
        do
        {
            interrupted = FALSE;
            
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                debugLog("oggenc() interrupted");
                interrupted = TRUE;
            }
            
        } while ((size > 0 && pos < 255 && buf[pos] != '\r' && buf[pos] != '\n') || interrupted);
        buf[pos] = '\0';

        if (sscanf(buf, "\t[\t%d.%d%%]", &sector, &end) == 2)
        {
            *progress = (double)(sector + (end*0.1))/100;
        }
        else if (sscanf(buf, "\t[\t%d,%d%%]", &sector, &end) == 2)
        {
            *progress = (double)(sector + (end*0.1))/100;
        }
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (oggenc_pid != 0)
    {
        debugLog("w6\n");
        usleep(100000);
    }
    *progress = 1;
}

// uses opusenc to encode a WAV file into a OGG and tag it
//
// tracknum - the track number
// artist - the artist's name
// album - the album the song came from
// title - the name of the song
// wavfilename - the path to the WAV file to encode
// opusfilename - the path to the output OPUS file
// bitrate - the bitrate to encode at
// progress - the percent done
void opusenc(int tracknum,
            char * artist,
            char * album,
            char * title,
            char * year,
            char * genre,
            char * wavfilename,
            char * opusfilename,
            int bitrate,
            double * progress)
{
    int fd;

    int pos;
    char bitrate_text[4];
    char tracknum_text[16];
    char album_text[128];
    char year_text[32];
    char genre_text[64];
    const char * args[19];

    snprintf(bitrate_text, 4, "%d", int_to_bitrate(bitrate,FALSE));

    pos = 0;
    args[pos++] = "opusenc";
    args[pos++] = "--bitrate";
    args[pos++] = bitrate_text;

    if ((tracknum > 0) && (tracknum < 100))
    {
        snprintf(tracknum_text,16,"TRACKNUMBER=%d",tracknum);
        args[pos++] = "--comment";
        args[pos++] = tracknum_text;
    }
    if ((artist != NULL) && (strlen(artist) > 0))
    {
        args[pos++] = "--artist";
        args[pos++] = artist;
    }
    if ((album != NULL) && (strlen(album) > 0))
    {
        snprintf(album_text,128,"ALBUM=%s",album);
        args[pos++] = "--comment";
        args[pos++] = album_text;
    }
    if ((title != NULL) && (strlen(title) > 0))
    {
        args[pos++] = "--title";
        args[pos++] = title;
    }
    if ((year != NULL) && (strlen(year) > 0))
    {
        snprintf(year_text,32,"DATE=%s",year);
        args[pos++] = "--comment";
        args[pos++] = year_text;
    }
    if ((genre != NULL) && (strlen(genre) > 0))
    {
        snprintf(genre_text,64,"GENRE=%s",genre);
        args[pos++] = "--comment";
        args[pos++] = genre_text;
    }
    args[pos++] = wavfilename;
    args[pos++] = opusfilename;
    args[pos++] = NULL;

    fd = exec_with_output(args, STDERR_FILENO, &oggenc_pid, NULL);

    int opussize;
    char opusbuf[256];
    do
    {
        /* The opus encoder doesn't give me an estimate for completion
        * or any way to estimate it myself, just the number of seconds
        * done. So just sit in here until the program exits */
        opussize = read(fd, &opusbuf[0], 256);

        if (opussize == -1 && errno == EINTR)
        /* signal interrupted read(), try again */
            opussize = 1;
    } while (opussize > 0);

    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (oggenc_pid != 0)
    {
        debugLog("w12\n");
        usleep(100000);
    }
    *progress = 1;
}


// uses the FLAC reference encoder to encode a WAV file into a FLAC and tag it
//
// tracknum - the track number
// artist - the artist's name
// albumartist - we want to tag this if it is a compilation
// album - the album the song came from
// title - the name of the song
// wavfilename - the path to the WAV file to encode
// flacfilename - the path to the output FLAC file
// compression_level - how hard to compress the file (0-8) see flac man page
// progress - the percent done
void flac(int tracknum,
          char * artist,
          char * albumartist, //mw
          gboolean single_artist, //mw
          char * album,
          char * title,
          char * genre,
          char * year,
          char * wavfilename,
          char * flacfilename,
          int compression_level,
          double * progress)
{
    int fd;

    char buf[256];
    int size;
    int pos;
    
    int sector;
    
    char tracknum_text[16];
    char * artist_text = NULL;
    char * albumartist_text = NULL; //mw
    char * album_text = NULL;
    char * title_text = NULL;
    char * genre_text = NULL;
    char * year_text = NULL;
    char compression_level_text[3];
    const char * args[21];
    
    snprintf(tracknum_text, 16, "TRACKNUMBER=%d", tracknum);
    
    if(artist != NULL)
    {
        artist_text = malloc(strlen(artist) + 8);
        if (artist_text == NULL)
            fatalError("malloc(sizeof(char) * (strlen(artist)+8)) failed. Out of memory.");
        snprintf(artist_text, strlen(artist) + 8, "ARTIST=%s", artist);
    }
    
    //mw
    if((albumartist != NULL) && (!single_artist))
    {
        albumartist_text = malloc(strlen(albumartist) + 13);
        if (albumartist_text == NULL)
            fatalError("malloc(sizeof(char) * (strlen(albumartist)+13)) failed. Out of memory.");
        snprintf(albumartist_text, strlen(albumartist) + 13, "ALBUMARTIST=%s", albumartist);
    }
    //mw end
    
    if(album != NULL)
    {
        album_text = malloc(strlen(album) + 7);
        if (album_text == NULL)
            fatalError("malloc(sizeof(char) * (strlen(album)+7)) failed. Out of memory.");
        snprintf(album_text, strlen(album) + 7, "ALBUM=%s", album);
    }
    
    if(title != NULL)
    {
        title_text = malloc(strlen(title) + 7);
        if (title_text == NULL)
            fatalError("malloc(sizeof(char) * (strlen(title)+7) failed. Out of memory.");
        snprintf(title_text, strlen(title) + 7, "TITLE=%s", title);
    }
    
    if(genre != NULL)
    {
        genre_text = malloc(strlen(genre) + 7);
        if (genre_text == NULL)
            fatalError("malloc(sizeof(char) * (strlen(genre)+7) failed. Out of memory.");
        snprintf(genre_text, strlen(genre) + 7, "GENRE=%s", genre);
    }
    
    if(year != NULL)
    {
        year_text = malloc(strlen(year) + 6);
        if (year_text == NULL)
            fatalError("malloc(sizeof(char) * (strlen(year)+6) failed. Out of memory.");
        snprintf(year_text, strlen(year) + 6, "DATE=%s", year);
    }
    
    snprintf(compression_level_text, 3, "-%d", compression_level);
    
    pos = 0;
    args[pos++] = "flac";
    args[pos++] = "-f";
    args[pos++] = compression_level_text;
    if ((tracknum > 0) && (tracknum < 100))
    {
        args[pos++] = "-T";
        args[pos++] = tracknum_text;
    }
    if ((artist != NULL) && (strlen(artist) > 0))
    {
        args[pos++] = "-T";
        args[pos++] = artist_text;
    }

    //mw
    if ((albumartist != NULL) && (!single_artist) && (strlen(albumartist) > 0))
    {
        args[pos++] = "-T";
        args[pos++] = albumartist_text;
    }
    //mw end

    if ((album != NULL) && (strlen(album) > 0))
    {
        args[pos++] = "-T";
        args[pos++] = album_text;
    }
    if ((title != NULL) && (strlen(title) > 0))
    {
        args[pos++] = "-T";
        args[pos++] = title_text;
    }
    if ((genre != NULL) && (strlen(genre) > 0))
    {
        args[pos++] = "-T";
        args[pos++] = genre_text;
    }
    if ((year != NULL) && (strlen(year) > 0))
    {
        args[pos++] = "-T";
        args[pos++] = year_text;
    }
    args[pos++] = wavfilename;
    args[pos++] = "-o";
    args[pos++] = flacfilename;
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &flac_pid, NULL);
    
    free(artist_text);
    free(album_text);
    free(albumartist_text);
    free(title_text);
    free(genre_text);
    free(year_text);
    
    do
    {
        pos = -1;
        bool interrupted;
        do
        {
            interrupted = FALSE;
            
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                debugLog("flac() interrupted");
                interrupted = TRUE;
            }
            
        } while ((size > 0 && pos < 255 && buf[pos] != '\r' && buf[pos] != '\n' && buf[pos] != '\b') || interrupted);
        buf[pos] = '\0';

        if (sscanf(buf, "%d%% ", &sector) == 1)
        {
            *progress = (double)sector/100;
        }
    } while (size > 0);
    
    close(fd);
    
    /* don't go on until the signal for the previous call is handled */
    while (flac_pid != 0)
    {
        debugLog("w7\n");
        usleep(100000);
    }
    *progress = 1;
}

void wavpack(int tracknum,
             char* wavfilename,
             int compression,
             bool hybrid,
             int bitrate,
             double* progress)
{
    const char* args[10];
    int fd;
    int pos;
    int size;
    char buf[256];
    
    pos = 0;
    args[pos++] = "wavpack";
    
    if(hybrid)
    {
        char bitrateTxt[7];
        snprintf(bitrateTxt, 7, "-b%d", bitrate);
        args[pos++] = bitrateTxt;
        
        args[pos++] = "-c";
    }
    
    args[pos++] = "-y";
    if(compression == 0)
        args[pos++] = "-f";
    else if(compression == 2)
        args[pos++] = "-h";
    else if(compression == 3)
        args[pos++] = "-hh";
    // default is no parameter (normal compression)
    
    args[pos++] = "-x3";
    
    args[pos++] = wavfilename;
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &wavpack_pid, NULL);
    
    do
    {
        pos = -1;
        bool interrupted;
        do
        {
            interrupted = FALSE;
            
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                debugLog("wavpack() interrupted");
                interrupted = TRUE;
            }
        } while ((size > 0 && pos < 255 && buf[pos] != '\b') || interrupted);
        
        buf[pos] = '\0';
        
        for (; pos>0; pos--)
        {
            if (buf[pos] == ',')
            {
                pos++;
                break;
            }
        }
        
        int percent;
        /* That extra first condition is because wavpack ends encoding with a
        * line like this:
        * created 01 - Unknown Artist - Track 1.wv (+.wvc) in 50.22 secs (lossless, 73.91%)
        */
        if (buf[strlen(buf) - 1] != ')' && sscanf(&buf[pos], "%d%%", &percent) == 1)
        {
            *progress = (double)percent/100;
            //!! This was commented out, possibly because the last line is in some 
            // different format than the normal progress
            //~ fprintf(stderr, "line '%s' percent %.2lf\n", &buf[pos], *progress);
        }
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (wavpack_pid != 0)
    {
        debugLog("w8\n");
        usleep(100000);
    }
    *progress = 1;
}

void mac(char* wavfilename,
         char* monkeyfilename,
         int compression,
         double* progress)
{
    const char* args[5];
    int fd;
    int pos;
    
    pos = 0;
    args[pos++] = "mac";
    args[pos++] = wavfilename;
    args[pos++] = monkeyfilename;
    
    char compressParam[10];
    snprintf(compressParam, 10, "-c%d", compression);
    args[pos++] = compressParam;
    
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &monkey_pid, NULL);
    
    int size;
    char buf[256];
    do
    {
        pos = -1;
        do
        {
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                size = 1;
            }
            
        } while ((buf[pos] != '\r') && (buf[pos] != '\n') && (size > 0) && (pos < 255));
        buf[pos] = '\0';
        
        double percent;
        if (sscanf(buf, "Progress: %lf", &percent) == 1)
        {
            *progress = percent / 100;
        }
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (monkey_pid != 0)
    {
        debugLog("w9\n");
        usleep(100000);
    }
    *progress = 1;
}

void musepack(char* wavfilename,
              char* musepackfilename,
              int quality,
              double* progress)
{
    const char* args[7];
    int fd;
    int pos;
    
    pos = 0;
    args[pos++] = "mpcenc";
    args[pos++] = "--overwrite";
    
    args[pos++] = "--quality";
    char qualityParam[6];
    snprintf(qualityParam, 6, "%d.00", quality);
    args[pos++] = qualityParam;
    
    args[pos++] = wavfilename;
    args[pos++] = musepackfilename;
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &musepack_pid, NULL);
    
    int size;
    char buf[256];
    do
    {
        pos = -1;
        bool interrupted;
        do
        {
            interrupted = FALSE;
            
            pos++;
            size = read(fd, &buf[pos], 1);
            
            if (size == -1 && errno == EINTR)
            /* signal interrupted read(), try again */
            {
                pos--;
                debugLog("musepack() interrupted");
                interrupted = TRUE;
            }
            
        } while ((size > 0 && pos < 255 && buf[pos] != '\r' && buf[pos] != '\n') || interrupted);
        buf[pos] = '\0';
        
        double percent;
        if (sscanf(buf, " %lf", &percent) == 1)
        {
            *progress = percent / 100;
        }
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (musepack_pid != 0)
    {
        debugLog("w10\n");
        usleep(100000);
    }
    *progress = 1;
}

void aac(int tracknum,
         char * artist,
         char * album,
         char * title,
         char * genre,
         char * year,
         char* wavfilename,
         char* aacfilename,
         int quality,
         double* progress)
{
    const char* args[9];
    char* dynamic_args[6];
    int fd;
    int pos;
    int dyn_pos;
    
    pos = 0;
    args[pos++] = "neroAacEnc";
    
    args[pos++] = "-q";
    char qualityParam[5];
    snprintf(qualityParam, 5, "0.%d", quality);
    args[pos++] = qualityParam;
    
    args[pos++] = "-if";
    args[pos++] = wavfilename;
    args[pos++] = "-of";
    args[pos++] = aacfilename;
    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &aac_pid, NULL);
    
    int size;
    char buf[256];
    do
    {
        /* The Nero encoder doesn't give me an estimate for completion
        * or any way to estimate it myself, just the number of seconds
        * done. So just sit in here until the program exits */
        size = read(fd, &buf[0], 256);
        
        if (size == -1 && errno == EINTR)
        /* signal interrupted read(), try again */
            size = 1;
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (aac_pid != 0)
    {
        debugLog("w11\n");
        usleep(100000);
    }

    /* Now add tags to the encoded track */

    pos = 0;
    args[pos++] = "neroAacTag";

    args[pos++] = aacfilename;

    dyn_pos = 0;
    if (asprintf(&dynamic_args[dyn_pos], "-meta:artist=%s", artist) > 0)
    {
        args[pos++] = dynamic_args[dyn_pos++];
    }
    if (asprintf(&dynamic_args[dyn_pos], "-meta:title=%s", title) > 0)
    {
        args[pos++] = dynamic_args[dyn_pos++];
    }
    if (asprintf(&dynamic_args[dyn_pos], "-meta:album=%s", album) > 0)
    {
        args[pos++] = dynamic_args[dyn_pos++];
    }
    if (asprintf(&dynamic_args[dyn_pos], "-meta:year=%s", year) > 0)
    {
        args[pos++] = dynamic_args[dyn_pos++];
    }
    if (asprintf(&dynamic_args[dyn_pos], "-meta:genre=%s", genre) > 0)
    {
        args[pos++] = dynamic_args[dyn_pos++];
    }
    if (asprintf(&dynamic_args[dyn_pos], "-meta:track=%d", tracknum) > 0)
    {
        args[pos++] = dynamic_args[dyn_pos++];
    }

    args[pos++] = NULL;
    
    fd = exec_with_output(args, STDERR_FILENO, &aac_pid, NULL);
    
    do
    {
        /* The Nero tag writer doesn't take very long to run. Just slurp the output. */
        size = read(fd, &buf[0], 256);
        
        if (size == -1 && errno == EINTR)
        /* signal interrupted read(), try again */
            size = 1;
    } while (size > 0);
    
    close(fd);
    /* don't go on until the signal for the previous call is handled */
    while (aac_pid != 0)
    {
        debugLog("w12\n");
        usleep(100000);
    }

    while(dyn_pos)
    {
        free(dynamic_args[--dyn_pos]);
    }
    *progress = 1;
}
