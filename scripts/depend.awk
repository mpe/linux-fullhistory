# This is an awk script which does dependencies. We do NOT want it to
# recursivly follow #include directives.
#
# The HPATH environment variable should be set to indicate where to look
# for include files.  The -I infront of the path is optional.

#
# Surely there is a more elegant way to see if a file exists.  Anyone know
# what it is?
#
function fileExists(f,    TMP, dummy, result) {
	if(result=FILEHASH[f]) {
		if(result=="Yes") {
			return "Yes"
		} else {return ""}
	}
	ERRNO = getline dummy < f
	if(ERRNO >= 0) {
		close(f)
		return FILEHASH[f]="Yes"
	} else {
		FILEHASH[f]="No"
		return ""
	}
}

function endfile(f) {
	if (hasconfig && !needsconfig) {
		printf "%s doesn't need config\n",f > "/dev/stderr"
	}
	if (hasdep) {
		print cmd
	}
}

BEGIN{
	hasdep=0
	hasconfig=0
	needsconfig=0
	if(!(TOPDIR=ENVIRON["TOPDIR"])) {
		print "Environment variable TOPDIR is not set"
		exit 1
	}
	split(ENVIRON["HPATH"],parray," ")
	for(path in parray) {
	    sub("^-I","",parray[path])
	    sub("[/ ]*$","",parray[path])
	}
}

/^[ 	]*#[ 	]*if.*[^A-Za-z_]CONFIG_/ {
	needsconfig=1
	if (!hasconfig) {
		printf "%s needs config but has not included config file\n",FILENAME > "/dev/stderr"
		# only say it once per file..
		hasconfig = 1
	}
}

/^[ 	]*#[ 	]*include[ 	]*[<"][^ 	]*[>"]/{
	found=0
	if(LASTFILE!=FILENAME) {
		endfile(LASTFILE)
		hasdep=0
		hasconfig=0
		needsconfig=0
		cmd=""
		LASTFILE=FILENAME
		depname=FILENAME
		relpath=FILENAME
		sub("\\.c",".o: ",depname)
		sub("\\.S",".o: ",depname)
		if (depname==FILENAME) {
			cmd="\n\t@touch "depname
		}
		sub("\\.h",".h: ",depname)
		if(relpath ~ "^\\." ) {
			sub("[^/]*$","",  relpath)
			relpath=relpath"/"
			sub("//","/",  relpath)
		} else {
			relpath=""
		}
	}
	fname=$0
	sub("^#[ 	]*include[ 	]*[<\"]","",fname)
	sub("[>\"].*","",fname)
	if (fname=="linux/config.h") {
		hasconfig=1
	}
	if(fileExists(relpath""fname)) {
		found=1
		if (!hasdep) {
			printf "%s", depname
		}
		hasdep=1
		printf " \\\n   %s", relpath""fname
		if(fname ~ "^\\." ) {
			if(!relpath in ARGV) {
				ARGV[ARGC]=relpath""fname
				++ARGC
			}
		}
	} else {
		for(path in  parray) {
			if(fileExists(parray[path]"/"fname)) {
				shortp=parray[path]
				found=1
				if (!hasdep) {
					printf "%s", depname
				}
				hasdep=1
				printf " \\\n   %s", parray[path]"/"fname
			}
		}
	}
}

END{
	endfile(FILENAME)
}
