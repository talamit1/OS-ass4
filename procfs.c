
#include "types.h"
#include "stat.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"


#define BLOCKSTATINUM 201
#define INODESTATINUM 202
#define PROCDIRINUM	  300
#define PROCDIR         0
#define	PROC_STATUS     1
#define PROC_CWD   	    2
#define PROC_FDINFO     3
#define FIRST_FD_FILE   4
#define LAST_FD_FILE   19




  static char *states[] = {
    [UNUSED]    "unused",
    [EMBRYO]    "embryo",
    [SLEEPING]  "sleep ",
    [RUNNABLE]  "runble",
    [RUNNING]   "run   ",
    [ZOMBIE]    "zombie"
  };  


struct fd {
  uint type;
  int ref; // reference count
  char readable;
  char writable;
  uint inum;
  uint off;
};

struct ptable{
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

struct blockstat {
  uint total_blocks;
  uint free_blocks;
  uint num_of_access;
  uint num_of_hits;
};

struct proc_stat{
  enum procstate state;
  uint size;

};

struct inodestat {
  ushort total;
  ushort free;
  ushort valid;
  uint refs;
  ushort used;
};

int whichProcFile(int inum ,int type);
int isFDFile(int inum);
int procRead(struct inode* ip,char *dst,int off,int n);
int inodestatRead(struct inode* ip,char *dst,int off,int n);
int procPIDDirRead(struct inode* ip,char *dst,int off,int n);
int blockstatRead(struct inode* ip,char *dst,int off,int n);
//int readProcCWD(struct inode* ip,char *dst,int off,int n);
int readProcStat(struct inode* ip,char *dst,int off,int n);
int readProcFdinfo(struct inode* ip,char *dst,int off,int n);
int readProcFdFile(struct inode* ip,char *dst,int off,int n);
int itoa(int num, char *str);
int adjustRead(void* buf, uint bytes_read, char *dst, int off, int n);
void blockstatToString(struct blockstat* blockstat,char* buffer);
void inodestatToString(struct inodestat* inodestat,char* buffer);
void statusToString(struct proc_stat* ps,char* buf);




int 
procfsisdir(struct inode *ip) {
  //cprintf("checking inod number:%d\n",ip->inum );
  int ans=ip->type==T_DEV&&
  ip->major==PROCFS && 
  		 (whichProcFile(ip->inum,PROCDIR)|| 
  		  whichProcFile(ip->inum,PROC_FDINFO)||
  		 namei("proc")->inum == ip->inum);
  //cprintf("ans is: %d\n",ans );
  return ans;
}

void 
procfsiread(struct inode* dp, struct inode *ip) {
	if(ip->inum<200)
		return;
	ip->type=T_DEV;
	ip->major=PROCFS;
	ip->flags|=I_VALID;


}

int
procfsread(struct inode *ip, char *dst, int off, int n) {
	
	if(namei("proc")->inum==ip->inum)
		return procRead(ip,dst,off,n); //working good

	else if(ip->inum==BLOCKSTATINUM)
		return blockstatRead(ip,dst,off,n); //working good
		
	else if(ip->inum==INODESTATINUM)
		return inodestatRead(ip,dst,off,n);	//working good

	else if(whichProcFile(ip->inum,PROCDIR))
		return procPIDDirRead(ip,dst,off,n);

	else if(whichProcFile(ip->inum,PROC_STATUS))
		return readProcStat(ip,dst,off,n);
		

	else if(whichProcFile(ip->inum,PROC_CWD))
		//return readProcCWD(ip,dst,off,n);
		return 0;

	else if(whichProcFile(ip->inum,PROC_FDINFO))
		return readProcFdinfo(ip,dst,off,n);

	else if(isFDFile(ip->inum))
		return readProcFdFile(ip,dst,off,n);
		//return 0;

  panic("unknown file type");

  return 0;
}

int whichProcFile(int inum ,int type){
	int ans=0;
	for(int i=0;i<64;i++){
		if(inum==PROCDIRINUM+type+100*i)
			ans= 1;
	}
	return ans;
}

int isFDFile(int inum){
	if(inum > 300 && inum < 6900){
		return ((inum % 100) < 20 && (inum%100) > 3);
	}
	return 0;
}



int
procfswrite(struct inode *ip, char *buf, int n)
{
  panic("tring to write to a read-only device");
  return 0;
}



/*-------------------------------start of imnplemntung the cases functions---------------*/

/*----------------------PROC DEVICE ROOT FOLDER-------------------*/
int procRead(struct inode* ip,char *dst,int off,int n){
	struct proc* p;
  	struct ptable* pt;
	struct dirent dirent;
	int i;

	uint curr_proc=4;
	uint ind=off/sizeof(dirent);

	switch(ind){

	//currentDir
	case 0:

		dirent.inum=ip->inum;
		strncpy(dirent.name, ".",sizeof("."));
		break;
	//Parent Directory
	case 1:
		dirent.inum=namei("/")->inum;
		strncpy(dirent.name,"..",strlen(".."));
		break;
	//Blockstat case
	case 2:
		dirent.inum=BLOCKSTATINUM;
		strncpy(dirent.name,"blockstat",strlen("blockstat"));
		break;
	//inode stat cas
	case 3:
		dirent.inum=INODESTATINUM;
		strncpy(dirent.name,"inodestat",strlen("inodestat"));
		break;

	//proc folders case
	default:
		pt = getPtable();
		acquire(&pt->lock);
		for(i=0;i<NPROC;i++){
			p=pt->proc +i;
		 	if(p->state != UNUSED && p->state != ZOMBIE && ind == curr_proc++){
		 		if(itoa(p->pid,dirent.name)==-1)
		 			panic("itoa failed");
		 		dirent.inum=PROCDIRINUM+i*100;
		 		break;
			}
		}
		
		release(&pt->lock);
      		if(i >= NPROC)
       		 return 0;
	}

	memmove(dst,&dirent,n);
	return n;
}
/*----------------------PROC NUM PID DIR READ-------------------*/
int procPIDDirRead(struct inode* ip,char *dst,int off,int n){
	struct proc* p;
  	struct ptable* pt;
	struct dirent dirent;
	int index;
	pt = getPtable();
	
	if(off == 0){
      index = 0;
  } else
      index = off/sizeof(dirent);


	switch(index){
		//current dir case
		case 0:
			dirent.inum=ip->inum;
			strncpy(dirent.name, ".",sizeof("."));
			break;
		//parent dir case
		case 1:
			dirent.inum = namei("proc")->inum;
			strncpy(dirent.name,"..\0",sizeof(".."));
			break;
		//cwd
		case 2:
			acquire(&pt->lock);
			p=pt->proc+ ((ip->inum - PROCDIRINUM) /100);
			if(p->state == UNUSED || p->state == ZOMBIE){
				release(&pt->lock);
			    return 0;
			}
			dirent.inum=p->cwd->inum;
			release(&pt->lock);
			strncpy(dirent.name,"cwd",sizeof("cwd"));
			break;
		//fdinfo
		case 3:
			acquire(&pt->lock);
			p=pt->proc + ((ip->inum-PROCDIRINUM)/100);
			if(p->state == UNUSED || p->state == ZOMBIE){
				release(&pt->lock);
			    return 0;
			}
			strncpy(dirent.name,"fdinfo",sizeof("fdinfo"));
			dirent.inum=ip->inum+PROC_FDINFO;
			release(&pt->lock);
			break;
		//status case
		case 4:
			acquire(&pt->lock);
			p=pt->proc+ ((ip->inum-PROCDIRINUM) /100);
			if(p->state == UNUSED || p->state == ZOMBIE){
				release(&pt->lock);
			    return 0;
			}
			strncpy(dirent.name,"status",sizeof("status"));
			dirent.inum=ip->inum+PROC_STATUS;
			release(&pt->lock);
			break;
		default:
			 return 0;
		}
	
	memmove(dst,&dirent,n);
	return n;
}
/*----------------------END OF PROC NUM PID DIR READ-------------------*/


/*----------------------FDINFO FOKDER READ-------------------*/
int readProcFdinfo(struct inode* ip,char *dst,
					int off,int n){
  struct dirent dirent;
  uint index, fd, proc_num;
  struct ptable* pt;
  struct proc* p;
  int count_fds=2;

    if(off == 0){
      index = 0;
  	} 
  else
      index = off/sizeof(dirent);

  switch(index){
	//current dir case
	case 0:
		dirent.inum=ip->inum;
		strncpy(dirent.name, ".",sizeof("."));
		break;
	//parent dit case
	case 1:
		dirent.inum=ip->inum - PROCDIRINUM - PROC_FDINFO;
		strncpy(dirent.name,"..",sizeof(".."));
		break;

	default:
		proc_num= (ip->inum- PROCDIRINUM - PROC_FDINFO)/100;
		pt=getPtable();
		acquire(&pt->lock);
		p=pt->proc+proc_num;
		if(p->state == UNUSED || p->state == ZOMBIE){
			release(&pt->lock);
	    	return 0;
		}
		for(fd = 0; fd<NOFILE; fd++){
        if(p->ofile[fd] && p->ofile[fd]->type==FD_INODE && index == count_fds++){
          if(itoa(fd, dirent.name) == -1)
            panic("procfsread: pid exceeds the max number of digits");
          dirent.inum = ip->inum+1+fd;    //initial fd files inodes
          break;
        }
      }
      release(&pt->lock);
      if(fd >= NOFILE)
        return 0;
   }
  memmove(dst, &dirent , n);
  return n;
}
/*----------------------END OF FDINFO FOKDER READ-------------------*/


/*----------------------BLOCKSTAT READ-------------------*/
int blockstatRead(struct inode *ip, char *dst, int off, int n)
{

  char buffer[BSIZE];
  struct blockstat blockstat;
  getBlockstat(&blockstat);

  blockstatToString(&blockstat,buffer);
 	

  return adjustRead(buffer, strlen(buffer), dst, off, n);
 }

 void blockstatToString(struct blockstat* blockstat,char* buffer){
  char free_blocks[11];
  char total_blocks[11];
  char hit_ratio[24];

  int free_blocks_length = itoa(blockstat->free_blocks, free_blocks);
  int total_blocks_length = itoa(blockstat->total_blocks, total_blocks);

  int hit_ratio_length = itoa(blockstat->num_of_hits, hit_ratio);
  strncpy(hit_ratio+hit_ratio_length, " / ", 3);
  hit_ratio_length += 3;
  hit_ratio_length += itoa(blockstat->num_of_access, hit_ratio + hit_ratio_length);

  strncpy(buffer, "Free Blocks: ", 14);
  strncpy(buffer+strlen(buffer), free_blocks, free_blocks_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
  strncpy(buffer+strlen(buffer), "Total Blocks: ", 15);
  strncpy(buffer+strlen(buffer), total_blocks, total_blocks_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
  strncpy(buffer+strlen(buffer), "Hit Ratio: ", 12);
  strncpy(buffer+strlen(buffer), hit_ratio, hit_ratio_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
 }
 /*----------------------END OF BLOCKSTAT READ-------------------*/

/*----------------------INODESTAT READ-------------------*/
int inodestatRead (struct inode *ip, char *dst, int off, int n){
  struct inodestat inodestat;
  char buffer[BSIZE];
  fillInodeStat(&inodestat);
  inodestatToString(&inodestat,buffer);
  return adjustRead(buffer, strlen(buffer), dst, off, n);
}


void inodestatToString(struct inodestat* inodestat,char* buffer){
  char free_inodes[3];
  char valid_inodes[3];
  char refs_per_inode[16];

  int free_inodes_length = itoa(inodestat->free, free_inodes);
  int valid_inodes_length = itoa(inodestat->valid, valid_inodes);

  int refs_per_inode_length = itoa(inodestat->refs, refs_per_inode);
  strncpy(refs_per_inode+refs_per_inode_length, " / ", 3);
  refs_per_inode_length += 3;
  refs_per_inode_length += itoa(inodestat->used, refs_per_inode + refs_per_inode_length);

  strncpy(buffer, "Free Inodes: ", 14);
  strncpy(buffer+strlen(buffer), free_inodes, free_inodes_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
  strncpy(buffer+strlen(buffer), "Valid Inodes: ", 15);
  strncpy(buffer+strlen(buffer), valid_inodes, valid_inodes_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
  strncpy(buffer+strlen(buffer), "Refs Per Inode: ", 17);
  strncpy(buffer+strlen(buffer), refs_per_inode, refs_per_inode_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
}

/*----------------------END OF INODESTAT READ-------------------*/


/*-----------------PROC/PID/STATUS case------------------*/
int readProcStat(struct inode* ip,char *dst,int off,int n){
  struct ptable* pt;
  struct proc* p;
  struct proc_stat procstat;
  pt=getPtable();
  acquire(&pt->lock);
  	p=pt->proc+ (ip->inum -PROCDIRINUM - PROC_STATUS)/100;        //convert the inum in to the ptable index and add it to ptable to get proc
  	procstat.size=p->sz;
  	procstat.state=p->state;
  release(&pt->lock);
  
  char buf[BSIZE];

  statusToString(&procstat,buf);

  return adjustRead(buf,strlen(buf),dst,off,n);
}

void statusToString(struct proc_stat* pstatus,char* buffer){
	
	char* firstStr="process is in state: ";
	char* secondstr="process size is: ";

  char proc_size[11];
  int proc_size_length = itoa(pstatus->size, proc_size);

  strncpy(buffer, firstStr, strlen(firstStr)+1);
  strncpy(buffer+strlen(buffer), states[pstatus->state], strlen(states[pstatus->state])+1);
  strncpy(buffer+strlen(buffer), "\n", 2);
  strncpy(buffer+strlen(buffer), secondstr, strlen(secondstr)+1);
  strncpy(buffer+strlen(buffer), proc_size, proc_size_length+1);
  strncpy(buffer+strlen(buffer), "\n", 2);


}
/*-----------------END OF PROC/PID/STATUS case------------------*/





int adjustRead(void* buf, uint bytes_read, char *dst, int off, int n)
{
  int bytes_to_send = 0;
  if (off < bytes_read) {
    bytes_to_send = bytes_read - off;
    if (bytes_to_send < n) {
      memmove(dst,(char*)buf+off,bytes_to_send);
      return bytes_read;
    }
    memmove(dst,(char*)buf+off,n);
    return n;
  }
  return 0;
}



int
itoa(int num, char *str)
{
  int temp, len, i;
	temp = num;
	len = 1;
	while (temp/10!=0){
		len++;
		temp /= 10;
	}
  if(len > DIRSIZ){
    cprintf("pidToString: Directory name should not exceed %d characters but this PID exceeds %d digits", DIRSIZ, DIRSIZ);
    return -1;
  }
	for (i = len; i > 0; i--){
		str[i-1] = (num%10)+48;
		num/=10;
	}
	str[len]='\0';

  return len;
}


int readProcFdFile(struct inode* ip,char *dst,int off,int n){
  uint  fdInd;
  struct proc* p;
  fdInd=(ip->inum%100)-PROC_FDINFO-1;
   //cprintf("readProcFdFile !! inod number is:%d\n",ip->inum);
   //cprintf("readProcFdFile !! fdInd is:%d\n",fdInd);
  struct ptable* pt;
  struct file* fd;
  struct fd pfd;
  pt = getPtable();
  acquire(&pt->lock);
  	p=pt->proc + (ip->inum - PROCDIRINUM - PROC_FDINFO -fdInd+1)/100;
  	if(p->state == UNUSED || p->state == ZOMBIE){
      release(&pt->lock);
      return 0;
    }
    fd = p->ofile[fdInd];
    pfd.type = fd->type;
    pfd.ref = fd->ref;
    pfd.readable = fd->readable;
    pfd.writable = fd->writable;
    if(fd->type == FD_INODE)
      pfd.inum = fd->ip->inum;
    pfd.off = fd->off;
  release(&pt->lock);


   return(adjustRead(&pfd,sizeof(pfd),dst,off,n));
}




void
procfsinit(void)
{
  devsw[PROCFS].isdir = procfsisdir;
  devsw[PROCFS].iread = procfsiread;
  devsw[PROCFS].write = procfswrite;
  devsw[PROCFS].read = procfsread;
}


