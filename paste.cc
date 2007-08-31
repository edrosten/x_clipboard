#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <map>
#include <stdio.h>
#include <iostream>
using namespace std;

/*
	
Copying and pasting is in general a difficult problem: the application doing the
pasting has to first know where to get the data from, and then the two
applications (probably written by different people, maybe running on different
computers without a shared filesystem) communicate data in a format they bot
understand even though they are different applications.

The first three problems are solved by the X server: it mediates the
communication in a standard way. The last problem is solved by providing a
mechanism that allows the two programs to negotiate which data format to
transfer data in. Esentially, the pasting application asks for a list of
available formats, and then picks the one it deems most suitable. Unfortunately,
if both applications can grok types which are nearly equivalent, (such as
multiple image types), there is no way of telling which is best.

Anyway, in order to understand the details of how to operate this mechanism, a
little background is required.



A bit of background.
--------------------

Atoms
-----

The server contains a list of Atoms. An atom is a short string with an
assosciated number. The purpose of this is to avoid passing around
and comparing strings. It is done much more efficiently with atoms
instead, since only the 4 byte integer ID needs to be passsed and compared.

XInternAtom gets the atom number corresponding to a string.
XGetAtomName gets the string corresponding ot the atom number.

There is a single global list of atoms on the X server.


Properties
----------

_EACH_ window has a list of properties. Each list element contains an arbitrary
bunch of data with a numeric ID, a data type and a format type.  Unsuprisingly,
atoms are used to give names to these numeric IDs. In other words, the property
list is indexed by atoms---it's a list of name/value pairs. The data type is a
string containing a brief textual description of the data (eg a MIME type may be
used). Unsuprisingly again, this string is stored in an atom.  The format type
is the number of bits per element of the data and is either 8, 16 or 32.

The property data is read by XGetWindowProperty.

Certain (all?) properties can be written by any other program. So properties are
used to pass chunks of data between programs. This is how the clipboard works.


Selections
----------

If a bit of data is copied in one application, then the application grabs a
selection. There can be any number of selections, but there are several
predefined ones. Each selection has a name (ie an atom) identifing it. The two
useful selections are PRIMARY and CLIPBOARD. Highlight/middle click goes via
PRIMARY and explicit copy/paste goes via CLIPBOARD.

If you want to paste, then you need to get the selected stuff from the program
which owns the selection, and get it to convert it in to a format which you can
use. For this, you use XConvertSelection. But how does it know which format to
convert it in to? You tell it the name (ie atom) of the format you want. But how
do you know what to ask for? Well, first, you ask for a meta-format called
TARGETS. This causes the program to send you a list of the format names (atoms)
which it is able to convert to. You can then pick a suitable one from the list
and ask for it. When you ask for data using XConvertSelection, the program you 
with the data received a SelectionNotify event.

All converted data is communicated via a property on the destination window.
This means you must have a window, but it does not have to be mapped.  You get
to choose which property you wish it to be communicated via. Once the property
has been filled up with the data, the program XSendEvent's you a SelectionNotify
event to tell you that the data is ready to be read. Now you have successfully
pasted some data.


Drag 'n Drop with XDND
----------------------

This is very similar to pasting, since the same negotiation of data types must
occur. Instead of asking for TARGETS, you instead read XdndTypeList on the
source window[1]. Then you call XConvertSelection using the XdndSelection
clipboard.

Stepping back a bit, XDND postdates X11 by many years, so all communication is
done via the generic ClientMessage events (instead of SelectionNotify events).
Windows announce their ability to accept drops by setting the XdndAware property
on the window (the value is a single atom, containing the version number).

When something is dragged over you, you are first sent an XdndEnter event. This
tells you the version number. This may also contain the first 3 types in the
XdndTypeList property, but it may not. This is the point at which XdndTypeList
should be read.

You will then be sent a stream of XdndPosition events. These will contain the
action requested and the position. You must reply with an XdndStatus message
stating whether a drop _could_ occur, and what action it will occur with.

Eventually, you will get an XdndLeave event, or an XdndDrop event. In the latter
case, you then call XConvertSelection. When the data arrives, you send an
XdndFinished event back.

Looking at it from the other side, if you initiate a drag, then the first thing
you do is grab the mouse. Since the mouse is grabbed, other programs will not
receive events, so you must send XDnD events to the correct window. When your
mouse pointer enters a window with the XDndAware property, you will send that
window an XDnDEnter event, informing it that a drag is in progress, and which
datatypes you can provide. As the mouse moves, you send XdndPosition events, and
the application replies with XdndStatus. If a drop is possible, then you will
change the mouse pointer to indicate this. If you leave the XdndAware window,
you send an XdndLeave event, and if the mouse button is released, you ungrab the
mouse and send an XdndDrop event (if a drop is possible).


[1] XDnD also provides the first three targets in the first message it sends.
If it offers three or fewer targets, it may not provide XdndTypeList.

And here's how specifically:
*/



//Convert an atom name in to a std::string
string GetAtomName(Display* disp, Atom a)
{
	if(a == None)
		return "None";
	else
		return XGetAtomName(disp, a);
}

struct Property
{
	unsigned char *data;
	int format, nitems;
	Atom type;
};


//This atom isn't provided by default
Atom XA_TARGETS;


//This fetches all the data from a property
Property read_property(Display* disp, Window w, Atom property)
{
	Atom actual_type;
	int actual_format;
	unsigned long nitems;
	unsigned long bytes_after;
	unsigned char *ret=0;
	
	int read_bytes = 1024;	

	//Keep trying to read the property until there are no
	//bytes unread.
	do
	{
		if(ret != 0)
			XFree(ret);
		XGetWindowProperty(disp, w, property, 0, read_bytes, False, AnyPropertyType,
							&actual_type, &actual_format, &nitems, &bytes_after, 
							&ret);

		read_bytes *= 2;
	}while(bytes_after != 0);
	
	cerr << endl;
	cerr << "Actual type: " << GetAtomName(disp, actual_type) << endl;
	cerr << "Actual format: " << actual_format << endl;
	cerr << "Number of items: " << nitems <<  endl;

	Property p = {ret, actual_format, nitems, actual_type};

	return p;
}


// This function takes a list of targets which can be converted to (atom_list, nitems)
// and a list of acceptable targets with prioritees (datatypes). It returns the highest
// entry in datatypes which is also in atom_list: ie it finds the best match.
Atom pick_target_from_list(Display* disp, Atom* atom_list, int nitems, map<string, int> datatypes)
{
	Atom to_be_requested = None;
	//This is higger than the maximum priority.
	int priority=INT_MAX;

	for(int i=0; i < nitems; i++)
	{
		string atom_name = GetAtomName(disp, atom_list[i]);
		cerr << "Type " << i << " = " << atom_name << endl;
	
		//See if this data type is allowed and of higher priority (closer to zero)
		//than the present one.
		if(datatypes.find(atom_name)!= datatypes.end())
			if(priority > datatypes[atom_name])
			{	
				cerr << "Will request type: " << atom_name << endl;
				priority = datatypes[atom_name];
				to_be_requested = atom_list[i];
			}
	}

	return to_be_requested;
}

// Finds the best target given up to three atoms provided (any can be None).
// Useful for part of the Xdnd protocol.
Atom pick_target_from_atoms(Display* disp, Atom t1, Atom t2, Atom t3, map<string, int> datatypes)
{
	Atom atoms[3];
	int  n=0;

	if(t1 != None)
		atoms[n++] = t1;

	if(t2 != None)
		atoms[n++] = t2;

	if(t3 != None)
		atoms[n++] = t3;

	return pick_target_from_list(disp, atoms, n, datatypes);
}


// Finds the best target given a local copy of a property.
Atom pick_target_from_targets(Display* disp, Property p, map<string, int> datatypes)
{
	//The list of targets is a list of atoms, so it should have type XA_ATOM
	//but it may have the type TARGETS instead.

	if((p.type != XA_ATOM && p.type != XA_TARGETS) || p.format != 32)
	{ 
		//This would be really broken. Targets have to be an atom list
		//and applications should support this. Nevertheless, some
		//seem broken (MATLAB 7, for instance), so ask for STRING
		//next instead as the lowest common denominator

		if(datatypes.count("STRING"))
			return XA_STRING;
		else
			return None;
	}
	else
	{
		Atom *atom_list = (Atom*)p.data;
		
		return pick_target_from_list(disp, atom_list, p.nitems, datatypes);
	}
}



int main(int argc, char ** argv)
{
	
	Display* disp;
	Window root, w, drop_window;
	int screen;
	XEvent e;
	
	//The usual Xinit stuff...
	disp = XOpenDisplay(NULL);
	screen = DefaultScreen(disp);
	root = RootWindow(disp, screen);

	int do_xdnd=0;

	//Process commandline args

	//This is the kind of data we're prepared to select
	//Each argument corresponds to a type, in order of preference
	//The key is the type the data is the priority (bigger int is lower)
	map<string, int> datatypes;

	//The first command line argument selects the buffer.
	//by default we use PRIMARY, the only other option
	//which is normally sensible is CLIPBOARD
	Atom sel = XInternAtom(disp, "PRIMARY", 0);

	if(argc > 1)
		if(argv[1] == string("-dnd"))
			do_xdnd = 1;
		else if(argv[1] == string("-dndroot"))
			do_xdnd = 2;
		else
			sel = XInternAtom(disp, argv[1], 0);
	
	for(int i=2; i < argc; i++)	
	{
		datatypes[argv[i]] = i;
	}
	
	//The default if there is no command line argument
	if(datatypes.empty())
		datatypes["STRING"] = 1;


	//We need a target window for the pasted data to be sent to.
	//However, this does not need to be mapped.
	w = XCreateSimpleWindow(disp, root, 0, 0, 100, 100, 0, BlackPixel(disp, screen), BlackPixel(disp, screen));

	//Atoms for Xdnd
	Atom XdndEnter = XInternAtom(disp, "XdndEnter", False);
	Atom XdndPosition = XInternAtom(disp, "XdndPosition", False);
	Atom XdndStatus = XInternAtom(disp, "XdndStatus", False);
	Atom XdndTypeList = XInternAtom(disp, "XdndTypeList", False);
	Atom XdndActionCopy = XInternAtom(disp, "XdndActionCopy", False);
	Atom XdndDrop = XInternAtom(disp, "XdndDrop", False);
	Atom XdndLeave = XInternAtom(disp, "XdndLeave", False);
	Atom XdndFinished = XInternAtom(disp, "XdndFinished", False);
	Atom XdndSelection = XInternAtom(disp, "XdndSelection", False);
	Atom XdndProxy = XInternAtom(disp, "XdndProxy", False);


	if(do_xdnd)
	{
		if(do_xdnd == 1)
		{
			//If we're doing DnD, instead of normal paste, then we need a window to drop in.
			XMapWindow(disp, w);
			drop_window = w;
		}
		else if(do_xdnd == 2)
		{
			//Set up the root window
			XGrabServer(disp);
			//Check for the existence of XdndProxy
			Property p = read_property(disp, root, XdndProxy);
			
			if(p.type == None)
			{
				//Property does not exist, so set it to redirect to me
				XChangeProperty(disp, root, XdndProxy, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&w, 1);
				//Set the proxy on me to point to me (as per the spec)
				XChangeProperty(disp, w, XdndProxy, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&w, 1);
			}
			else
			{
				if(p.type == XA_WINDOW && p.format == 32 && p.nitems == 1)
					cerr << "Root already proxied to 0x" << hex << *(unsigned int*)p.data << endl;
				else
					cerr << "Root already proxied to <malformed>\n";

				return 4;
			}

			XUngrabServer(disp);
			drop_window = root;
		}

		//Announce XDND support
		Atom XdndAware = XInternAtom(disp, "XdndAware", False);
		Atom version=5;
		XChangeProperty(disp, w, XdndAware, XA_ATOM, 32, PropModeReplace, (unsigned char*)&version, 1);
	}


	//This is a meta-format for data to be "pasted" in to.
	//Requesting this format acquires a list of possible
	//formats from the application which copied the data.
	XA_TARGETS = XInternAtom(disp, "TARGETS", False);




	if(!do_xdnd)
	{
		//Request a list of possible conversions, if we're pasting.
		XConvertSelection(disp, sel, XA_TARGETS, sel, w, CurrentTime);
	}


	XFlush(disp);


	Atom to_be_requested = None;
	bool sent_request = 0;
	int xdnd_version=0;
	Window xdnd_source_window=None;

	for(;;)
	{
		XNextEvent(disp, &e);
		
		if(e.type == ClientMessage)
		{
			cerr << "A ClientMessage has arrived:\n";
			cerr << "Type = " << GetAtomName(disp, e.xclient.message_type) << " (" << e.xclient.format << ")\n";


			if(e.xclient.message_type == XdndEnter)
			{
				bool more_than_3 = e.xclient.data.l[1] & 1;
				Window source = e.xclient.data.l[0];

				cerr << hex << "Source window = 0x" << source << dec << endl;
				cerr << "Supports > 3 types = " << (more_than_3) << endl;
				cerr << "Protocol version = " << ( e.xclient.data.l[1] >> 24) << endl;
				cerr << "Type 1 = " << GetAtomName(disp, e.xclient.data.l[2]) << endl;
				cerr << "Type 2 = " << GetAtomName(disp, e.xclient.data.l[3]) << endl;
				cerr << "Type 3 = " << GetAtomName(disp, e.xclient.data.l[4]) << endl;

				xdnd_version = ( e.xclient.data.l[1] >> 24);

				//Query which conversions are available and pick the best

				if(more_than_3)
				{
					//Fetch the list of possible conversions
					//Notice the similarity to TARGETS with paste.
					Property p = read_property(disp, source , XdndTypeList);
					to_be_requested = pick_target_from_targets(disp, p, datatypes);
					XFree(p.data);
				}
				else
				{
					//Use the available list
					to_be_requested = pick_target_from_atoms(disp, e.xclient.data.l[2], e.xclient.data.l[3], e.xclient.data.l[4], datatypes);
				}


				cerr << "Requested type = " << GetAtomName(disp, to_be_requested) << endl;
			}
			else if(e.xclient.message_type == XdndPosition)
			{
				cerr << hex << "Source window = 0x" << e.xclient.data.l[0] << dec << endl;
				cerr << "Position: x=" << (e.xclient.data.l[2]  >> 16) << " y=" << (e.xclient.data.l[2] &0xffff)  << endl;
				cerr << "Timestamp = " << e.xclient.data.l[3] << " (Version >= 1 only)\n";
				
				Atom action=XdndActionCopy;
				if(xdnd_version >= 2)
					action = e.xclient.data.l[4];

				cerr << "Action = " << GetAtomName(disp, action) << " (Version >= 2 only)\n";
				

				//Xdnd: reply with an XDND status message
				XClientMessageEvent m;
				memset(&m, sizeof(m), 0);
				m.type = ClientMessage;
				m.display = e.xclient.display;
				m.window = e.xclient.data.l[0];
				m.message_type = XdndStatus;
				m.format=32;
				m.data.l[0] = drop_window;
				m.data.l[1] = (to_be_requested != None);
				m.data.l[2] = 0; //Specify an empty rectangle
				m.data.l[3] = 0;
				m.data.l[4] = XdndActionCopy; //We only accept copying anyway.

				XSendEvent(disp, e.xclient.data.l[0], False, NoEventMask, (XEvent*)&m);
				XFlush(disp);
			}
			else if(e.xclient.message_type == XdndLeave)
			{
				//to_be_requested = None;

				//We can't actually reset to_be_requested, since OOffice always
				//sends this event, even when it doesn't mean to.
				cerr << "Xdnd cancelled.\n";
			}
			else if(e.xclient.message_type == XdndDrop)
			{
				if(to_be_requested == None)
				{
					//It's sending anyway, despite instructions to the contrary.
					//So reply that we're not interested.
					XClientMessageEvent m;
					memset(&m, sizeof(m), 0);
					m.type = ClientMessage;
					m.display = e.xclient.display;
					m.window = e.xclient.data.l[0];
					m.message_type = XdndFinished;
					m.format=32;
					m.data.l[0] = drop_window;
					m.data.l[1] = 0;
					m.data.l[2] = None; //Failed.
					XSendEvent(disp, e.xclient.data.l[0], False, NoEventMask, (XEvent*)&m);
				}
				else
				{
					xdnd_source_window = e.xclient.data.l[0];
					if(xdnd_version >= 1)
						XConvertSelection(disp, XdndSelection, to_be_requested, sel, w, e.xclient.data.l[2]);
					else
						XConvertSelection(disp, XdndSelection, to_be_requested, sel, w, CurrentTime);
				}
			}

			cerr << endl;
		}

		if(e.type == SelectionNotify)
		{
			Atom target = e.xselection.target;

			cerr << "A selection notify has arrived!\n";
			cerr << hex << "Requestor = 0x" << e.xselectionrequest.requestor << dec << endl;
			cerr << "Selection atom = " << GetAtomName(disp, e.xselection.selection) << endl;	
			cerr << "Target atom    = " << GetAtomName(disp, target)    << endl;	
			cerr << "Property atom  = " << GetAtomName(disp, e.xselection.property) << endl;

			if(e.xselection.property == None)
			{
				//If the selection can not be converted, quit with error 2.
				//If TARGETS can not be converted (nothing owns the selection)
				//then quit with code 3.
				return 2 + (target == XA_TARGETS);
			}
			else 
			{
				Property prop = read_property(disp, w, sel);

				//If we're being given a list of targets (possible conversions)
				if(target == XA_TARGETS && !sent_request)
				{
					sent_request = 1;
					to_be_requested = pick_target_from_targets(disp, prop, datatypes);

					if(to_be_requested == None)
					{
						cerr << "No matching datatypes.\n";
						return 1;
					}
					else //Request the data type we are able to select
					{
						cerr << "Now requsting type " << GetAtomName(disp, to_be_requested) << endl;
						XConvertSelection(disp, sel, to_be_requested, sel, w, CurrentTime);
					}
				}
				else if(target == to_be_requested)
				{
					//Dump the binary data
					cerr << "Data begins:" << endl;
					cerr << "--------\n";
					cout.write((char*)prop.data, prop.nitems * prop.format/8);
					cout << flush;
					cerr << endl << "--------" << endl << "Data ends\n";

					if(do_xdnd)
					{	
						//Reply OK.
						XClientMessageEvent m;
						memset(&m, sizeof(m), 0);
						m.type = ClientMessage;
						m.display = disp;
						m.window = xdnd_source_window;
						m.message_type = XdndFinished;
						m.format=32;
						m.data.l[0] = w;
						m.data.l[1] = 1;
						m.data.l[2] = XdndActionCopy; //We only ever copy.

						//Reply that all is well.
						XSendEvent(disp, xdnd_source_window, False, NoEventMask, (XEvent*)&m);

						//Un-proxy the root window
						if(do_xdnd == 2)
							XDeleteProperty(disp, root, XdndProxy);

						XSync(disp, False);
					}

					return 0;
				}
				else return 0;

				XFree(prop.data);
			}
			cerr << endl;
		}
	}
}
