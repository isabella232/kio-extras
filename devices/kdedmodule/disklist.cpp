/*
 * disklist.cpp
 *
 * $Id$
 *
 * Copyright (c) 1999 Michael Kropfberger <michael.kropfberger@gmx.net>
 *
 * Requires the Qt widget libraries, available at no cost at
 * http://www.troll.no/
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <config.h>

#include <math.h>
#include <stdlib.h>

#include <kapplication.h>

#include "disklist.h"

#define BLANK ' '
#define DELIMITER '#'
#define FULL_PERCENT 95.0

#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif


/***************************************************************************
  * constructor
**/
DiskList::DiskList(QObject *parent, const char *name)
    : QObject(parent,name)
{
  mountPointExclusionList.setAutoDelete(true);
  loadExclusionLists();

   disks = new Disks;
   disks->setAutoDelete(TRUE);

   // BackgroundProcesses ****************************************
      dfProc = new KProcess(); Q_CHECK_PTR(dfProc);
       connect( dfProc, SIGNAL(receivedStdout(KProcess *, char *, int) ),
           this, SLOT (receivedDFStdErrOut(KProcess *, char *, int)) );
       //connect( dfProc, SIGNAL(receivedStderr(KProcess *, char *, int) ),
       //  this, SLOT (receivedDFStdErrOut(KProcess *, char *, int)) );
       connect(dfProc,SIGNAL(processExited(KProcess *) ),
              this, SLOT(dfDone() ) );

       readingDFStdErrOut=FALSE;
       //readFSTAB();
       //readDF();
};


/***************************************************************************
  * destructor
**/
DiskList::~DiskList()
{
    delete dfProc;
};


void DiskList::loadExclusionLists()
{
  QString val;
  KConfig cfg("mountwatcher");
  cfg.setGroup("mountpoints");
  for (int i=0;(!(val=cfg.readEntry(QString("exclude%1").arg(i),"")).isEmpty());i++)
	  mountPointExclusionList.append(new QRegExp(val));
}


/***************************************************************************
  * tries to figure out the possibly mounted fs
**/
int DiskList::readFSTAB()
{
  if (readingDFStdErrOut || dfProc->isRunning()) return -1;

#ifdef HAVE_SETMNTENT

#define SETMNTENT setmntent
#define ENDMNTENT endmntent
#define STRUCT_MNTENT struct mntent *
#define STRUCT_SETMNTENT FILE *
#define GETMNTENT(file, var) ((var = getmntent(file)) != 0)
#define MOUNTPOINT(var) var->mnt_dir
#define MOUNTTYPE(var) var->mnt_type
#define MOUNTOPTIONS(var) var->mnt_opts
#define HASMNTOPT(var, opt) hasmntopt(var, opt)
#define FSNAME(var) var->mnt_fsname

  STRUCT_SETMNTENT fstab;
  if ((fstab = SETMNTENT(FSTAB, "r")) == 0)
     return -1;

  STRUCT_MNTENT fe;
  while (GETMNTENT(fstab, fe))
  {
      DiskEntry *disk = new DiskEntry();
      disk->setMounted(FALSE);
      disk->setDeviceName(QFile::decodeName(FSNAME(fe)));
      //kdDebug() << "    deviceName:    [" << disk->deviceName() << "]" << endl;
      disk->setMountPoint(QFile::decodeName(MOUNTPOINT(fe)));
      //kdDebug() << "    MountPoint:    [" << disk->mountPoint() << "]" << endl;
      disk->setFsType(QFile::decodeName(MOUNTTYPE(fe)));
      //kdDebug() << "    FS-Type:       [" << disk->fsType() << "]" << endl;
      disk->setMountOptions(QFile::decodeName(MOUNTOPTIONS(fe)));
      //kdDebug() << "    Mount-Options: [" << disk->mountOptions() << "]" << endl;
      if (!ignoreDisk(disk))
         replaceDeviceEntry(disk);
      else
         delete disk;
  }
  ENDMNTENT(fstab);

#else
  QFile f(FSTAB);
  if ( f.open(IO_ReadOnly) ) {
    QTextStream t (&f);
    QString s;
    DiskEntry *disk;

    //disks->clear(); // ############

    while (! t.eof()) {
      s=t.readLine();
      s=s.simplifyWhiteSpace();
      if ( (!s.isEmpty() ) && (s.find(DELIMITER)!=0) ) {
               // not empty or commented out by '#'
	//	kdDebug() << "GOT: [" << s << "]" << endl;
	disk = new DiskEntry();// Q_CHECK_PTR(disk);
        disk->setMounted(FALSE);
        disk->setDeviceName(s.left(s.find(BLANK)) );
            s=s.remove(0,s.find(BLANK)+1 );
	    //  kdDebug() << "    deviceName:    [" << disk->deviceName() << "]" << endl;
#ifdef _OS_SOLARIS_
            //device to fsck
            s=s.remove(0,s.find(BLANK)+1 );
#endif
         disk->setMountPoint(s.left(s.find(BLANK)) );
            s=s.remove(0,s.find(BLANK)+1 );
	    //kdDebug() << "    MountPoint:    [" << disk->mountPoint() << "]" << endl;
	    //kdDebug() << "    Icon:          [" << disk->iconName() << "]" << endl;
         disk->setFsType(s.left(s.find(BLANK)) );
            s=s.remove(0,s.find(BLANK)+1 );
	    //kdDebug() << "    FS-Type:       [" << disk->fsType() << "]" << endl;
         disk->setMountOptions(s.left(s.find(BLANK)) );
            s=s.remove(0,s.find(BLANK)+1 );
	    //kdDebug() << "    Mount-Options: [" << disk->mountOptions() << "]" << endl;
	 if (!ignoreDisk(disk))
	   replaceDeviceEntry(disk);
         else
           delete disk;

      } //if not empty
    } //while
    f.close();
  } //if f.open
#endif

  //  kdDebug() << "DiskList::readFSTAB DONE" << endl;
  return 1;
}

bool DiskList::ignoreDisk(DiskEntry *disk)
{
	bool ignore;
	if ( (disk->deviceName() != "none")
	      && (disk->fsType() != "swap")
	      && (disk->fsType() != "tmpfs")
	      && (disk->deviceName() != "tmpfs")
	      && (disk->mountPoint() != "/dev/swap")
	      && (disk->mountPoint() != "/dev/pts")
	      && (disk->mountPoint().find("/proc") != 0)
	      && (disk->deviceName().find("shm") == -1  ))
		ignore=false;
	else
		ignore=true;

	if (!ignore) {
		for (QRegExp *exp=mountPointExclusionList.getFirst();exp;exp=mountPointExclusionList.next())
		{
			kdDebug()<<"TRYING TO DO A REGEXP SEARCH"<<endl;
			if (exp->search(disk->mountPoint())!=-1)
			{
				kdDebug()<<"IGNORING BECAUSE OF REGEXP SEARCH"<<endl;
				return true;
			}
		}
	}

	return ignore;
}



/***************************************************************************
  * is called, when the df-command writes on StdOut or StdErr
**/
void DiskList::receivedDFStdErrOut(KProcess *, char *data, int len )
{

  /* ATTENTION: StdERR no longer connected to this...
   * Do we really need StdErr?? on HP-UX there was eg. a line
   * df: /home_tu1/ijzerman/floppy: Stale NFS file handle
   * but this shouldn't cause a real problem
   */

  QString tmp = QString(data) + QString("\0");  // adds a zero-byte
  tmp.truncate(len);

  dfStringErrOut.append(tmp);
}

/***************************************************************************
  * reads the df-commands results
**/
int DiskList::readDF()
{
  if (readingDFStdErrOut || dfProc->isRunning()) return -1;
  setenv("LANG", "en_US", 1);
  setenv("LC_ALL", "en_US", 1);
  setenv("LC_MESSAGES", "en_US", 1);
  setenv("LC_TYPE", "en_US", 1);
  setenv("LANGUAGE", "en_US", 1);
  dfStringErrOut=""; // yet no data received
  dfProc->clearArguments();
  (*dfProc) << DF_COMMAND << DF_ARGS;
  if (!dfProc->start( KProcess::NotifyOnExit, KProcess::AllOutput ))
  {
    kdWarning(7020)<<i18n("could not execute [%1]").arg(DF_COMMAND)<<endl;
    return 0;
  }

  return 1;
}


/***************************************************************************
  * is called, when the df-command has finished
**/
void DiskList::dfDone()
{
  readingDFStdErrOut=TRUE;
  for ( DiskEntry *disk=disks->first(); disk != 0; disk=disks->next() )
    disk->setMounted(FALSE);  // set all devs unmounted

  QTextStream t (dfStringErrOut, IO_ReadOnly);
  QString s=t.readLine();
  if ( ( s.isEmpty() ) || ( s.left(10) != "Filesystem" ) )
  {
    kdWarning(7020)<<QString("Error running df command, couldn't parse output")<<endl;
    return;
  }
  while ( !t.atEnd() ) {
    QString u,v;
    DiskEntry *disk;
    s=t.readLine();
    s=s.simplifyWhiteSpace();
    if ( !s.isEmpty() ) {
      disk = new DiskEntry(); Q_CHECK_PTR(disk);

      if (s.find(BLANK)<0)      // devicename was too long, rest in next line
	if ( !t.eof() ) {       // just appends the next line
            v=t.readLine();
            s=s.append(v.latin1() );
            s=s.simplifyWhiteSpace();
	    //kdDebug() << "SPECIAL GOT: [" << s << "]" << endl;
	 }//if silly linefeed

      //kdDebug() << "EFFECTIVELY GOT " << s.length() << " chars: [" << s << "]" << endl;

      disk->setDeviceName(s.left(s.find(BLANK)) );
      s=s.remove(0,s.find(BLANK)+1 );
      //kdDebug() << "    DeviceName:    [" << disk->deviceName() << "]" << endl;

      if (NO_FS_TYPE) {
	//kdDebug() << "THERE IS NO FS_TYPE_FIELD!" << endl;
         disk->setFsType("?");
      } else {
         disk->setFsType(s.left(s.find(BLANK)) );
         s=s.remove(0,s.find(BLANK)+1 );
      };
      //kdDebug() << "    FS-Type:       [" << disk->fsType() << "]" << endl;
      //kdDebug() << "    Icon:          [" << disk->iconName() << "]" << endl;

      u=s.left(s.find(BLANK));
      disk->setKBSize(u.toInt() );
      s=s.remove(0,s.find(BLANK)+1 );
      //kdDebug() << "    Size:       [" << disk->kBSize() << "]" << endl;

      u=s.left(s.find(BLANK));
      disk->setKBUsed(u.toInt() );
      s=s.remove(0,s.find(BLANK)+1 );
      //kdDebug() << "    Used:       [" << disk->kBUsed() << "]" << endl;

      u=s.left(s.find(BLANK));
      disk->setKBAvail(u.toInt() );
      s=s.remove(0,s.find(BLANK)+1 );
      //kdDebug() << "    Avail:       [" << disk->kBAvail() << "]" << endl;


      s=s.remove(0,s.find(BLANK)+1 );  // delete the capacity 94%
      s=s.stripWhiteSpace();
      disk->setMountPoint(s );
      //kdDebug() << "    MountPoint:       [" << disk->mountPoint() << "]" << endl;

      if ( (disk->kBSize() > 0) &&
		(!ignoreDisk(disk)))
	   {
        disk->setMounted(TRUE);    // its now mounted (df lists only mounted)
	replaceDeviceEntryMounted(disk);
      } else
	delete disk;

    }//if not header
  }//while further lines available

  readingDFStdErrOut=FALSE;
  emit readDFDone();
}


void DiskList::replaceDeviceEntryMounted(DiskEntry *disk)
{
	//I'm assuming that df always returns the real device name, not a symlink
	int pos = -1;
	for( u_int i=0; i<disks->count(); i++ ) {
		DiskEntry *item=disks->at(i);
		if ( ((
			(item->realDeviceName()==disk->deviceName()) ) ||
			((item->inodeType()==true) &&
			(disk->inodeType()==true) &&
			(disk->inode()==item->inode()))) &&
			(item->mountPoint()==disk->mountPoint()) ) {
			item->setMounted(TRUE);
			pos=i;
			break;
		}
	}
	if (pos==-1)
		disks->append(disk);
	else
		delete disk;

}

/***************************************************************************
  * updates or creates a new DiskEntry in the KDFList and TabListBox
**/
void DiskList::replaceDeviceEntry(DiskEntry *disk)
{
  //
  // The 'disks' may already already contain the 'disk'. If it do
  // we will replace some data. Otherwise 'disk' will be added to the list
  //

  //
  // 1999-27-11 Espen Sand:
  // I can't get find() to work. The Disks::compareItems(..) is
  // never called.
  //
  //int pos=disks->find(disk);

  kdDebug()<<"Trying to find an item referencing: "<<disk->deviceName()<<endl;
  int pos = -1;
  for( u_int i=0; i<disks->count(); i++ )
  {
    DiskEntry *item = disks->at(i);
    int res = disk->deviceName().compare( item->deviceName() );
    if( res == 0 )
    {
      res = disk->mountPoint().compare( item->mountPoint() );
    }
    if( res == 0 )
    {
      pos = i;
      break;
    }
  }

  if ((pos == -1) && (disk->mounted()) )
    // no matching entry found for mounted disk
    if ((disk->fsType() == "?") || (disk->fsType() == "cachefs")) {
      //search for fitting cachefs-entry in static /etc/vfstab-data
      DiskEntry* olddisk = disks->first();
      QString odiskName;
      while (olddisk != 0) {
        int p;
        // cachefs deviceNames have no / behind the host-column
	// eg. /cache/cache/.cfs_mnt_points/srv:_home_jesus
	//                                      ^    ^
        odiskName = olddisk->deviceName().copy();
        int ci=odiskName.find(':'); // goto host-column
        while ((ci =odiskName.find('/',ci)) > 0) {
           odiskName.replace(ci,1,"_");
        }//while
        // check if there is something that is exactly the tail
	// eg. [srv:/tmp3] is exact tail of [/cache/.cfs_mnt_points/srv:_tmp3]
        if ( ( (p=disk->deviceName().findRev(odiskName
	            ,disk->deviceName().length()) )
                != -1)
	      && (p + odiskName.length()
	          == disk->deviceName().length()) )
        {
             pos = disks->at(); //store the actual position
             disk->setDeviceName(olddisk->deviceName());
             olddisk=0;
	} else
          olddisk=disks->next();
      }// while
    }// if fsType == "?" or "cachefs"


#ifdef NO_FS_TYPE
  if (pos != -1) {
     DiskEntry * olddisk = disks->at(pos);
     if (olddisk)
        disk->setFsType(olddisk->fsType());
  }
#endif

  if (pos != -1) {  // replace
      DiskEntry * olddisk = disks->at(pos);
      if ( (-1!=olddisk->mountOptions().find("user")) &&
           (-1==disk->mountOptions().find("user")) ) {
          // add "user" option to new diskEntry
          QString s=disk->mountOptions();
          if (s.length()>0) s.append(",");
          s.append("user");
          disk->setMountOptions(s);
       }

      //FStab after an older DF ... needed for critFull
      //so the DF-KBUsed survive a FStab lookup...
      //but also an unmounted disk may then have a kbused set...
      if ( (olddisk->mounted()) && (!disk->mounted()) ) {
         disk->setKBSize(olddisk->kBSize());
         disk->setKBUsed(olddisk->kBUsed());
         disk->setKBAvail(olddisk->kBAvail());
      }
      disks->remove(pos); // really deletes old one
      disks->insert(pos,disk);
  } else {
    disks->append(disk);
  }//if

}

#include "disklist.moc"






