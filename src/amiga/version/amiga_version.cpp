/***************************************************************************
 *   Copyright (C) 2017 by Alexander Fritsch                               *
 *   email: selco@t-online.de                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write see:                           *
 *               <http://www.gnu.org/licenses/>.                           *
 ***************************************************************************/



#define      VERSION       "1"
#define      REVISION      "1"                     /* Revision always starts with 1 ! */
//#define      DATE          "15.07.2017"   /* comes from make-command line as CXXFLAGS+=-DDATE=\\\"$(date +'%d.%m.%Y')\\\" */
#define      PROGNAME      "espeak"
/*#define      COMMENT       "BETA-Version, Alexander Fritsch"*/
#define      COMMENT       "Alexander Fritsch, selco, based on espeak-1.48.15 by Jonathan Duddington"

#define      VERS          PROGNAME" " VERSION "." REVISION
#define      VERSTAG       "\0$VER: " PROGNAME " " VERSION "." REVISION " (" DATE ") " COMMENT

char versiontag[] = VERSTAG;
