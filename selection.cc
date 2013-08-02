#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <map>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
using namespace std;

//See paste.cc for a description of how the copy/paste and XDnD state machine works.

//See process_selection_request to see how to perform a paste when a SelectionNotify
//event arrives.

//see main for a sample implementation of an Xdnd state machine.


//Define atome not defined in Xatom.h

Atom XA_TARGETS;
Atom XA_multiple;
Atom XA_image_bmp;
Atom XA_image_jpg;
Atom XA_image_tiff;
Atom XA_image_png;
Atom XA_text_uri_list;
Atom XA_text_uri;
Atom XA_text_plain;
Atom XA_text;

Atom XA_XdndSelection;
Atom XA_XdndAware;
Atom XA_XdndEnter;
Atom XA_XdndLeave;
Atom XA_XdndTypeList;
Atom XA_XdndPosition;
Atom XA_XdndActionCopy;
Atom XA_XdndStatus;
Atom XA_XdndDrop;
Atom XA_XdndFinished;

//The three states of Xdnd: we're over a window which does not
//know about XDnD, we're over a window which does know, but won't
//allow a drop (because we offer no suitable datatype), or we're 
//over a window which will accept a drop.
#define UNAWARE 0
#define UNRECEPTIVE 1
#define CAN_DROP 2

//Utility function for getting the atom name as a string.
string GetAtomName(Display* disp, Atom a)
{
	if(a == None)
		return "None";
	else
		return XGetAtomName(disp, a);
}


//A simple, inefficient function for reading a 
//whole file in to memory
string read_whole_file(const string& name, string& fullname)
{
	ostringstream f;
	ifstream file;

	//Try in the current directory first, then in the data directory
	{
		vector<char> buf(4096, 0);
		getcwd(&buf[0], 4095);
		fullname = &buf[0] + string("/") + name;
	}	

	file.open(fullname.c_str(), ios::binary);

	if(!file.good())
	{
		fullname = DATADIR + name;
		file.open(fullname.c_str(), ios::binary);
	}

	f << file.rdbuf();
	
	return f.str();
}


//Construct a list of targets and place them in the specified property This
//consists of all datatypes we know of as well as TARGETS and MULTIPLE. Reading
//this property tell the application wishing to paste which datatypes we offer.
void set_targets_property(Display* disp, Window w, map<Atom, string>& typed_data, Atom property)
{

	vector<Atom> targets; targets.push_back(XA_TARGETS);
	targets.push_back(XA_multiple);


	for(map<Atom,string>::const_iterator i=typed_data.begin(); i != typed_data.end(); i++)
		targets.push_back(i->first);

		
	cout << "Offering: ";
	for(unsigned int i=0; i < targets.size(); i++)
		cout << GetAtomName(disp, targets[i]) << "  ";
	cout << endl;

	//Fill up this property with a list of targets.
	XChangeProperty(disp, w, property, XA_ATOM, 32, PropModeReplace, 
					(unsigned char*)&targets[0], targets.size());
}



//This function essentially performs the paste operation: by converting the
//stored data in to a format acceptable to the destination and replying
//with an acknowledgement.
void process_selection_request(XEvent e, map<Atom, string>& typed_data)
{

	if(e.type != SelectionRequest)
		return;

	//Extract the relavent data
	Window owner     = e.xselectionrequest.owner;
	Atom selection   = e.xselectionrequest.selection;
	Atom target      = e.xselectionrequest.target;
	Atom property    = e.xselectionrequest.property;
	Window requestor = e.xselectionrequest.requestor;
	Time timestamp   = e.xselectionrequest.time;
	Display* disp    = e.xselection.display;

	cout << "A selection request has arrived!\n";
	cout << hex << "Owner = 0x" << owner << endl;
	cout << "Selection atom = " << GetAtomName(disp, selection) << endl;	
	cout << "Target atom    = " << GetAtomName(disp, target)    << endl;	
	cout << "Property atom  = " << GetAtomName(disp, property) << endl;	
	cout << hex << "Requestor = 0x" << requestor << dec << endl;
	cout << "Timestamp = " << timestamp << endl;
	

	//X should only send requests for the selections since we own.
	//since we own exaclty one, we don't need to check it.

	//Replies to the application requesting a pasting are XEvenst
	//sent via XSendEvent
	XEvent s;

	//Start by constructing a refusal request.
	s.xselection.type = SelectionNotify;
	//s.xselection.serial     - filled in by server
	//s.xselection.send_event - filled in by server
	//s.xselection.display    - filled in by server
	s.xselection.requestor = requestor;
	s.xselection.selection = selection;
	s.xselection.target    = target;
	s.xselection.property  = None;   //This means refusal
	s.xselection.time      = timestamp;



	if(target ==XA_TARGETS)
	{
		cout << "Replying with a target list.\n";
		set_targets_property(disp, requestor, typed_data, property);
		s.xselection.property = property;
	}
	else if(typed_data.count(target))
	{
		//We're asked to convert to one the formate we know about
		cout << "Replying with which ever data I have" << endl;

		//Fill up the property with the URI.
		s.xselection.property = property;
		XChangeProperty(disp, requestor, property, target, 8, PropModeReplace, 
						reinterpret_cast<const unsigned char*>(typed_data[target].c_str()), typed_data[target].size());
	}
	else if(target == XA_multiple)
	{
		//In this case, the property has been filled up with a list
		//of atom pairs. The pairs being (target, property). The 
		//processing should continue as if whole bunch of
		//SelectionRequest events had been received with the 
		//targets and properties specified.

		//The ICCCM is rather ambiguous and confusing on this particular point,
		//and I've never encountered a program which requests this (I can't 
		//test it), so I haven't implemented it.

		cout << "MULTIPLE is not implemented. It should be, according to the ICCCM, but\n"
			 << "I've never encountered it, so I can't test it.\n";
	}
	else
	{	
		//We've been asked to converto to something we don't know 
		//about.
		cout << "No valid conversion. Replying with refusal.\n";
	}
	
	//Reply
	XSendEvent(disp, e.xselectionrequest.requestor, True, 0, &s);
	cout << endl;
}


//Find the applications top level window under the mouse.
Window find_app_window(Display* disp, Window w)
{
	//Drill down the windows under the mouse, looking for
	//the window with the XdndAware property.

	int nprops, i=0;
	Atom* a;

	if(w == 0)
		return 0;

	//Search for the WM_STATE property
	a = XListProperties(disp, w, &nprops);
	for(i=0; i < nprops; i++)
		if(a[i] == XA_XdndAware)
			break;

	if(nprops)
		XFree(a);

	if(i != nprops)
		return w;
	
	//Drill down one more level.
	Window child, wtmp;
	int tmp;
	unsigned int utmp;
	XQueryPointer(disp, w, &wtmp, &child, &tmp, &tmp, &tmp, &tmp, &utmp);

	return find_app_window(disp, child);
}




int main(int argc, char**argv)
{
	
	Display* disp;
	Window root, w;
	int screen;
	XEvent e;
	
	//Standard X init stuff
	disp = XOpenDisplay(NULL);
	screen = DefaultScreen(disp);
	root = RootWindow(disp, screen);

	//A window is required to perform copy/paste operations
	//but it does not need to be mapped.
	w = XCreateSimpleWindow(disp, root, 0, 0, 100, 100, 0, BlackPixel(disp, screen), BlackPixel(disp, screen));
	

	cerr << "Created window: 0x" << hex <<  w << dec << endl << endl;


	bool dnd=0;
	Atom selection  = XA_PRIMARY;


	//The 1st command line argument is the selection name. Default is PRIMARY
	//or alternatively, it can specify DnD operation.
	if(argc > 1)
	{
		if(argv[1] == string("-dnd"))
			dnd=1;
		else
			selection = XInternAtom(disp, argv[1], 0);
	}
		
	
	//None of these atoms are provided in Xatom.h
	XA_TARGETS = XInternAtom(disp, "TARGETS", False);
	XA_multiple = XInternAtom(disp, "MULTIPLE", False);
	XA_image_bmp = XInternAtom(disp, "image/bmp", False);
	XA_image_jpg = XInternAtom(disp, "image/jpeg", False);
	XA_image_tiff = XInternAtom(disp, "image/tiff", False);
	XA_image_png = XInternAtom(disp, "image/png", False);
	XA_text_uri_list = XInternAtom(disp, "text/uri-list", False);
	XA_text_uri= XInternAtom(disp, "text/uri", False);
	XA_text_plain = XInternAtom(disp, "text/plain", False);
	XA_text = XInternAtom(disp, "TEXT", False);
	XA_XdndSelection = XInternAtom(disp, "XdndSelection", False);
	XA_XdndAware = XInternAtom(disp, "XdndAware", False);
	XA_XdndEnter = XInternAtom(disp, "XdndEnter", False);
	XA_XdndLeave = XInternAtom(disp, "XdndLeave", False);
	XA_XdndTypeList = XInternAtom(disp, "XdndTypeList", False);
	XA_XdndPosition = XInternAtom(disp, "XdndPosition", False);
	XA_XdndActionCopy = XInternAtom(disp, "XdndActionCopy", False);
	XA_XdndStatus = XInternAtom(disp, "XdndStatus", False);
	XA_XdndDrop = XInternAtom(disp, "XdndDrop", False);
	XA_XdndFinished = XInternAtom(disp, "XdndFinished", False);
	
	//Create a mapping between the data type (specified as an atom) and the
	//actual data. The data consists of a prespecified list of files in the
	//current or install directory, and the URL of the PNG, in various
	//incarnations. 
	map<Atom, string> typed_data; string url;
	
	typed_data[XA_image_bmp] = read_whole_file("r0x0r.bmp", url);
	typed_data[XA_image_jpg] = read_whole_file("r0x0r.jpg", url);
	typed_data[XA_image_tiff] = read_whole_file("r0x0r.tiff", url);
	typed_data[XA_image_png] = read_whole_file("r0x0r.png", url);

	url = "file://" + url;

	typed_data[XA_text_uri_list] = url;
	typed_data[XA_text_uri] = url;
	typed_data[XA_text_plain] = url;
	typed_data[XA_text] = url;
	typed_data[XA_STRING] = url;

	
	
	if(dnd)
	{
		//We need to map the window to drag from
		XMapWindow(disp, w);	
		XSelectInput(disp, w, Button1MotionMask | ButtonReleaseMask);

		//We set this, so that TARGETS does not need to be called, as
		//specified by Xdnd.
		set_targets_property(disp, w, typed_data, XA_XdndTypeList);
	}
	else
	{
		//All your selection are belong to us...
		XSetSelectionOwner(disp, selection, w, CurrentTime);
	}

	XFlush(disp);

	
	bool dragging=0;                   //Are we currently dragging
	Window previous_window=0;          //Window found by the last MotionNotify event.
	int previous_version = -1;         //XDnD version of previous_window
	int status=UNAWARE;               
	

	//Create three cursors for the three different XDnD states.
	//I think a turkey is a good choice for a program which doesn't
	//understand Xdnd.
	Cursor grab_bad =XCreateFontCursor(disp, XC_gobbler);
	Cursor grab_maybe =XCreateFontCursor(disp, XC_circle);
	Cursor grab_good =XCreateFontCursor(disp, XC_sb_down_arrow);

	for(;;)
	{
		XNextEvent(disp, &e);
		
		//Wait until something asks for the selection or until we loose the selection.
		if(e.type == SelectionClear)
		{
			cout  << "SelectionClear event received. Quitting.\n";
			return 0;
		}
		else if(e.type == SelectionRequest)
		{
			//A request to paste has occured.
			process_selection_request(e, typed_data);
		}
		else if(e.type == MotionNotify && dragging == 0)
		{
			if(XGrabPointer(disp, w, True, Button1MotionMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, root, grab_bad, CurrentTime) == GrabSuccess)
			{
				dragging=1;
				XSetSelectionOwner(disp, XA_XdndSelection, w, CurrentTime);
				cout << "Begin dragging.\n\n";
			}
			else
				cout << "Grab failed!\n\n";
		}
		else if(e.type == MotionNotify)
		{
			cout << "Dragged pointer moved: " << endl;

			Window window=0;
			Atom atmp;
			int version=-1;
			int fmt;
			unsigned long nitems, bytes_remaining;
			unsigned char *data = 0;

			//Look for XdndAware in the window under the pointer. So, first, 
			//find the window under the pointer.
			window = find_app_window(disp, root);
			cout << "Application window is: 0x" << hex << window << dec << endl;
			
			

			if(window == previous_window)
				version = previous_version;
			else if(window == None)
				;
			else if(XGetWindowProperty(disp, window, XA_XdndAware, 0, 2, False, AnyPropertyType, &atmp, &fmt, &nitems, &bytes_remaining, &data) != Success)
				cout << "Property read failed.\n";
			else if(data == 0)
				cout << "Property read failed.\n";
			else if(fmt != 32)
				cout << "XdndAware should be 32 bits, not " << fmt << " bits\n";
			else if(nitems != 1)
				cout << "XdndAware should contain exactly 1 item, not " << nitems << " items\n";
			else
			{
				version = data[0];
				cout << "XDnD version is " << version << endl;
			}

			if(status == UNAWARE && version != -1)
				status = UNRECEPTIVE;
			else if(version == -1)
				status = UNAWARE;

			//Update the pointer state.
			if(status == UNAWARE)
				XChangeActivePointerGrab(disp, Button1MotionMask | ButtonReleaseMask, grab_bad, CurrentTime);
			else if(status == UNRECEPTIVE)
				XChangeActivePointerGrab(disp, Button1MotionMask | ButtonReleaseMask, grab_maybe, CurrentTime);
			else
				XChangeActivePointerGrab(disp, Button1MotionMask | ButtonReleaseMask, grab_good, CurrentTime);

			

			if(window != previous_window && previous_version != -1)
			{
				cout << "Left window 0x" << hex << previous_window  << dec << ": sending XdndLeave\n";
				//We've left an old, aware window.
				//Send an XDnD Leave 

				XClientMessageEvent m;
				memset(&m, sizeof(m), 0);
				m.type = ClientMessage;
				m.display = e.xclient.display;
				m.window = previous_window;
				m.message_type = XA_XdndLeave;
				m.format=32;
				m.data.l[0] = w;
				m.data.l[1] = 0;
				m.data.l[2] = 0;
				m.data.l[3] = 0;
				m.data.l[4] = 0;

				XSendEvent(disp, previous_window, False, NoEventMask, (XEvent*)&m);
				XFlush(disp);
			}

			if(window != previous_window && version != -1)
			{	
				cout << "Entered window 0x" << hex << window  << dec << ": sending XdndLeave\n";
				//We've entered a new, aware window.
				//Send an XDnD Enter event.
				map<Atom, string>::const_iterator i = typed_data.begin();

				XClientMessageEvent m;
				memset(&m, sizeof(m), 0);
				m.type = ClientMessage;
				m.display = e.xclient.display;
				m.window = window;
				m.message_type = XA_XdndEnter;
				m.format=32;
				m.data.l[0] = w;
				m.data.l[1] = min(5, version) << 24  |  (typed_data.size() > 3);
				m.data.l[2] = typed_data.size() > 0 ? i++->first : 0;
				m.data.l[3] = typed_data.size() > 1 ? i++->first : 0;
				m.data.l[4] = typed_data.size() > 2 ? i->first : 0;


				cout << "   version  = " << min(5, version) << endl
				     << "   >3 types = " << (typed_data.size() > 3) << endl
					 << "   Type 1   = " << GetAtomName(disp, m.data.l[2]) << endl
					 << "   Type 2   = " << GetAtomName(disp, m.data.l[3]) << endl
					 << "   Type 3   = " << GetAtomName(disp, m.data.l[4]) << endl;

				XSendEvent(disp, window, False, NoEventMask, (XEvent*)&m);
				XFlush(disp);
			}

			if(version != -1)
			{
				//Send an XdndPosition event.
				//
				// We're being abusive, and ignoring the 
				// rectangle of silence.


				int x, y, tmp;
				unsigned int utmp;
				Window wtmp;

				XQueryPointer(disp, window, &wtmp, &wtmp, &tmp, &tmp, &x, &y, &utmp);


				XClientMessageEvent m;
				memset(&m, sizeof(m), 0);
				m.type = ClientMessage;
				m.display = e.xclient.display;
				m.window = window;
				m.message_type = XA_XdndPosition;
				m.format=32;
				m.data.l[0] = w;
				m.data.l[1] = 0;
				m.data.l[2] = (x <<16) | y;
				m.data.l[3] = CurrentTime; //Our data is not time dependent, so send a generic timestamp;
				m.data.l[4] = XA_XdndActionCopy;

				cerr << "Sending XdndPosition" << endl
				     << "    x      = " << x << endl
				     << "    y      = " << y << endl
				     << "    Time   = " << m.data.l[3] << endl
					 << "    Action = " << GetAtomName(disp, m.data.l[4]) << endl;

				XSendEvent(disp, window, False, NoEventMask, (XEvent*)&m);
				XFlush(disp);

			}

			previous_window = window;	
			previous_version = version;
			cout << endl;
		}
		else if(dragging && e.type == ButtonRelease && e.xbutton.button == 1)
		{
			cout << "Mouse button was released.\n";


			if(status == CAN_DROP)
			{
				cout << "Perform drop:\n";

				XClientMessageEvent m;
				memset(&m, sizeof(m), 0);
				m.type = ClientMessage;
				m.display = e.xclient.display;
				m.window = previous_window;
				m.message_type = XA_XdndDrop;
				m.format=32;
				m.data.l[0] = w;
				m.data.l[1] = 0;
				m.data.l[2] = CurrentTime; //Our data is not time dependent, so send a generic timestamp;
				m.data.l[3] = 0;
				m.data.l[4] = 0;

				XSendEvent(disp, previous_window, False, NoEventMask, (XEvent*)&m);
				XFlush(disp);
			}


			XUngrabPointer(disp, CurrentTime);
			dragging=0;
			status=UNAWARE;
			previous_window=None;
			previous_version=-1;
			cout << endl;
		}
		else if(e.type == ClientMessage && e.xclient.message_type == XA_XdndStatus)
		{
			cout  << "XDnDStatus event received:" << endl
			      << "    Target window           = 0x" << hex << e.xclient.data.l[0] << dec << endl
			      << "    Will accept             = " << (e.xclient.data.l[1] & 1)  << endl
			      << "    No rectangle of silence = " << (e.xclient.data.l[1] & 2)  << endl
			      << "    Rectangle of silence x  = " << (e.xclient.data.l[2] >> 16)    << endl
			      << "    Rectangle of silence y  = " << (e.xclient.data.l[2] & 0xffff)    << endl
			      << "    Rectangle of silence w  = " << (e.xclient.data.l[3] >> 16)    << endl
			      << "    Rectangle of silence h  = " << (e.xclient.data.l[3] & 0xffff)    << endl
			      << "    Action                  = " << GetAtomName(disp, e.xclient.data.l[4]) << endl;

			
			if( (e.xclient.data.l[1] & 1) == 0 &&  e.xclient.data.l[4] != None)
			{
				cout << "Action is given, even though the target won't accept a drop.\n";
			}


			if(dragging)
			{
				if((e.xclient.data.l[1]&1) && status != UNAWARE)
					status = CAN_DROP;

				if(!(e.xclient.data.l[1]&1) && status != UNAWARE)
					status = UNRECEPTIVE;
			}

			if(!dragging)
				cout << "Message received, but dragging is not active!\n";

			if(status == UNAWARE)
				cout << "Message received, but we're not in an aware window!\n";

			cout << endl;
		}
		else if(e.type == ClientMessage && e.xclient.message_type == XA_XdndFinished)
		{
			//Check for these messages. Since out data is static, we don't need to do anything.
			cout  << "XDnDFinished event received:" << endl
			      << "    Target window           = 0x" << hex << e.xclient.data.l[0] << dec << endl
			      << "    Was successful          = " << (e.xclient.data.l[1] & 1)  << endl
			      << "    Action                  = " << GetAtomName(disp, e.xclient.data.l[2]) << endl;
			
			cout  << "No action performed.\n\n";
		}

	}

	return 0;
}

