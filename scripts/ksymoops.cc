/* ksymoops.c -- simple Linux Oops-log symbol resolver
   Copyright (C) 1995 Greg McGary
  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
  
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
  
   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to the
   Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* This is a simple filter to resolve EIP and call-trace symbols from
   a Linux kernel "Oops" log.  Supply the symbol-map file name as a
   command-line argument, and redirect the oops-log into stdin.
   Out will come the EIP and call-trace in symbolic form.  */

#include <fstream.h>
#include <string.h>
#include <stdlib.h>

inline int strequ(char const* x, char const* y) { return (::strcmp(x, y) == 0); }

//////////////////////////////////////////////////////////////////////////////

class KSym
{
    friend class NameList;

  private:
    long address_;
    char type_;
    char* name_;
    long offset_;
    long extent_;

  public:
    void set_extent(KSym const& next_ksym) { extent_ = next_ksym.address_ - address_; }
    friend istream& operator >> (istream&, KSym&);
    friend ostream& operator << (ostream&, const KSym&);
};

istream&
operator >> (istream& is, KSym& n)
{
    is >> hex >> n.address_;
    is >> n.type_;
    char name[128];
    is >> name;
    n.name_ = new char [strlen(name)+1];
    strcpy(n.name_, name);
    n.offset_ = 0;
    return is;
}

ostream&
operator << (ostream& os, const KSym& n)
{
    os << hex << n.address_ + n.offset_ << ' ' << n.type_ << ' ' << n.name_;
    if (n.offset_)
	os << '+' << hex << n.offset_ << '/' << hex << n.extent_;
    return os;
}

//////////////////////////////////////////////////////////////////////////////

class NameList
{
  private:
    // Caution: Fixed Allocation!
    // This should suffice for awhile since 1.1.86 has only 2482 symbols.
    KSym ksyms_0_[4096];
    int cardinality_;

  public:
    NameList() : cardinality_(0) { }
    
  public:
    KSym* find(long address);
    
  public:
    friend istream& operator >> (istream&, NameList&);
};

KSym*
NameList::find(long address)
{
    KSym* start = ksyms_0_;
    KSym* end = &ksyms_0_[cardinality_];
    KSym* mid;

    while (start <= end) {
	mid = &start[(end - start) / 2];
	if (mid->address_ < address)
	    start = mid + 1;
	else if (mid->address_ > address)
	    end = mid - 1;
	else
	    return mid;
    }
    while (mid->address_ > address)
	--mid;
    mid->offset_ = address - mid->address_;
    if (mid->offset_ > mid->extent_)
	clog << "Oops! " << *mid << endl;
    return mid;
}

istream&
operator >> (istream& is, NameList& n)
{
    KSym* ksyms = n.ksyms_0_;
    int cardinality = 0;
    while (!is.eof()) {
	is >> *ksyms;
	ksyms[-1].set_extent(*ksyms);
	ksyms++;
	cardinality++;
    }
    n.cardinality_ = --cardinality;
    return is;
}

//////////////////////////////////////////////////////////////////////////////

char const* program_name;

void
usage()
{
    clog << "Usage: " << program_name << " system-map-file < oops-log" << endl;
    exit(1);
}

int
main(int argc, char** argv)
{
    program_name = (argc--, *argv++);
    if (argc != 1)
	usage();

    char const* map_file_name = (argc--, *argv++);
    ifstream map(map_file_name);
    if (map.bad()) {
	clog << program_name << ": Can't open `" << map_file_name << "'" << endl;
	return 1;
    }
    
    NameList names;
    map >> names;

    char buffer[1024];
    while (!cin.eof())
    {
	long address;
	cin >> buffer;
	if (strequ(buffer, "EIP:")) {
	    cin >> hex >> address;
	    cin >> buffer[0];
	    cin >> hex >> address;
	    KSym* ksym = names.find(address);
	    if (ksym)
		cout << "EIP: " << *ksym << endl;
	} else if (strequ(buffer, "Trace:")) {
	    while ((cin >> address) && address > 0xc) {
		KSym* ksym = names.find(address);
		if (ksym)
		    cout << "Trace: " << *ksym << endl;
	    }
	    cout << endl;
	}
    }
    cout << flush;

    return 0;
}
