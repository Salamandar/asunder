/*
Asunder

Copyright(C) 2005 Eric Lathrop <eric@ericlathrop.com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

For more details see the file COPYING
*/
#include <gtk/gtk.h>
#include <cddb/cddb.h>
#include <stdbool.h>

enum
{
	COL_RIPTRACK,
	COL_TRACKNUM,
	COL_TRACKARTIST,
	COL_TRACKTITLE,
	COL_TRACKTIME,
	NUM_COLS
};

// scan the cdrom device for a disc
// returns True if a disc is present and
//   is different from the last time this was called
bool check_disc(char * cdrom);

void clear_widgets();

// creates a tree model that represents the data in the cddb_disc_t
GtkTreeModel * create_model_from_disc(cddb_disc_t * disc);

// open/close the drive's tray
void eject_disc(char * cdrom);

// looks up the given cddb_disc_t in the online database, and fills in the values
GList * lookup_disc(cddb_disc_t * disc);

// reads the TOC of a cdrom into a CDDB struct
// returns the filled out struct
// so we can send it over the internet to lookup the disc
cddb_disc_t * read_disc(char * cdrom);

// the main logic for scanning the discs
void refresh(char * cdrom, int force);

// updates all the necessary widgets with the data for the given cddb_disc_t
void update_tracklist(cddb_disc_t * disc);

extern GList * disc_matches;

extern GtkWidget * win_main;
extern GtkWidget * win_prefs;
extern GtkWidget * win_ripping;
extern GtkWidget * win_about;

extern GtkWidget * tracklist;

#define DEBUG
