/*****
 * picture.cc
 * Andy Hammerlindl 2002/06/06
 *
 * Stores a picture as a list of drawElements and handles its output to 
 * PostScript. 
 *****/

#include <csignal>

#include "errormsg.h"
#include "picture.h"
#include "util.h"
#include "settings.h"
#include "interact.h"

using std::string;
using std::list;
using std::ifstream;
using std::ofstream;

using namespace settings;

namespace camp {

const char *texready="(Please type a command or say `\\end')\n*";
iopipestream tex; // Bi-directional pipe to latex (to find label bbox)
  
picture::~picture()
{
}

// Find beginning of current layer.
list<drawElement*>::iterator picture::layerstart(list<bbox>::iterator& b)
{
  list<drawElement*>::iterator p;
  for(p=--nodes.end(), b=--bboxstack.end(); p != --nodes.begin(); --p, --b) {
    assert(*p);
    if((*p)->islayer()) break;
  }
  ++b;
  return ++p;
}

// Insert at beginning of current layer.
void picture::prepend(drawElement *P)
{
  assert(P);
  list<bbox>::iterator b;
  nodes.insert(layerstart(b),P);
  lastnumber=0;
}

void picture::append(drawElement *p)
{
  assert(p);
  nodes.push_back(p);
}

void picture::add(picture &pic)
{
  if (&pic == this) return;

  // STL's funny way of copying one list into another.
  copy(pic.nodes.begin(), pic.nodes.end(), back_inserter(nodes));
  copy(pic.bboxstack.begin(), pic.bboxstack.end(), back_inserter(bboxstack));
}

// Insert picture pic at beginning of current layer.
void picture::prepend(picture &pic)
{
  if (&pic == this) return;
  
  list<bbox>::iterator b;
  copy(pic.nodes.begin(), pic.nodes.end(), inserter(nodes, layerstart(b)));
  copy(pic.bboxstack.begin(), pic.bboxstack.end(),inserter(bboxstack,b));
  lastnumber=0;
}

bbox picture::bounds()
{
  size_t n=number();
  if(n == lastnumber) return b;
  
  list<drawElement*>::iterator p;
  
  if(!labels && settings::texprocess) {
    // Check to see if there are any labels yet
    p=nodes.begin();
    for(size_t i=0; i < lastnumber; ++i) ++p;
    for(; p != nodes.end(); ++p) {
      assert(*p);
      if((*p)->islabel())
        labels=true;
    }
  }
  
  if(labels) {
    drawElement::lastpen=pen(initialpen);
    if(!TeXinitialized) texinit();
  }
  
  p=nodes.begin();
  for(size_t i=0; i < lastnumber; ++i) ++p;
  for(; p != nodes.end(); ++p) {
    assert(*p);
    (*p)->bounds(b,tex,labelbounds,bboxstack);
  }

  lastnumber=n;
  return b;
}

void picture::texinit()
{
  tex.open("latex");
  texdocumentclass(tex);
  
  texpreamble(tex);

  tex << "\n";
  tex.wait(texready,"! ");
  TeXinitialized=true;
}
  
bool picture::texprocess(const string& texname, const string& outname,
			 const string& prefix, const bbox& bpos) 
{
  int status=0;
  std::ifstream outfile;
  
  outfile.open(texname.c_str());
  if(outfile) {
    outfile.close();
    ostringstream cmd;
    cmd << "latex \\scrollmode\\input " << texname;
    bool quiet=verbose <= 1;
    status=System(cmd,quiet);
    if(status) {
      if(quiet) status=System(cmd);
      return false;
    }
    string dviname=auxname(prefix,"dvi");
    double height=bpos.top-bpos.bottom;
    
    // Magic dvips offsets:
    double hoffset=-128.0;
    double voffset=(height < 11.0) ? -137.0+height : -126.0;
    
    if(origin != ZERO) {
      if(pdfformat || origin == BOTTOM) {
	voffset += max(pageHeight-(bpos.top-bpos.bottom+1.0),0.0);
      } else if(origin == CENTER) {
	hoffset += 0.5*max(pageWidth-(bpos.right-bpos.left+1.0),0.0);
	voffset += 0.5*max(pageHeight-(bpos.top-bpos.bottom+1.0),0.0);
      }
    }
    
    if(!pdfformat) {
      hoffset += postscriptOffset.getx();
      voffset -= postscriptOffset.gety();
    }

    string psname=auxname(prefix,"ps");
    ostringstream dcmd;
    dcmd << "dvips -R -t " << paperType << "size -O " << hoffset << "bp,"
	 << voffset << "bp";
    if(verbose <= 1) dcmd << " -q";
    dcmd << " -o " << psname << " " << dviname;
    status=System(dcmd);
    
    bbox bcopy=bpos;
    double hfuzz=0.1;
    double vfuzz=0.2;
    if(origin == CENTER || origin == TOP) {
      hfuzz *= 2.0; vfuzz *= 2.0;
    }
    
    bcopy.left -= hfuzz;
    bcopy.right += hfuzz;
    
    bcopy.bottom -= vfuzz;
    bcopy.top += vfuzz;
    
    ifstream fin(psname.c_str());
    ofstream fout(outname.c_str());
    string s;
    bool first=true;
    while(getline(fin,s)) {
      if(first && s.find("%%BoundingBox:") == 0) {
	if(verbose > 2) BoundingBox(cout,bpos);
	BoundingBox(fout,bcopy);
	first=false;
      } else fout << s << endl;
    }
    fout.close();
    
    if(!keep) { // Delete temporary files.
      unlink("texput.log");
      unlink(texname.c_str());
      unlink(dviname.c_str());
      unlink(psname.c_str());
      unlink(auxname(prefix,"aux").c_str());
      unlink(auxname(prefix,"log").c_str());
    }
  }
  if(status) return false;
  return true;
}

bool picture::postprocess(const string& epsname, const string& outname,
			  const string& outputformat, bool wait,
			  const bbox& bpos)
{
  int status=0;
  ostringstream cmd;
  
  if(!epsformat) {
    if(pdfformat) cmd << "gs -q -dNOPAUSE -dBATCH -sDEVICE=pdfwrite -dEPSCrop"
		      << " -dDEVICEWIDTHPOINTS=" 
		      << ceil(bpos.right-bpos.left+2.0)
		      << " -dDEVICEHEIGHTPOINTS=" 
		      << ceil(bpos.top-bpos.bottom+2.0)
		      << " -sOutputFile=" << outname << " " << epsname;
    else {
      double expand=2.0;
      double res=(tgifformat ? deconstruct : expand)*72.0;
      cmd << "convert -density " << res << "x" << res;
      if(!tgifformat) cmd << " +antialias -geometry " << 100.0/expand << "%x";
      cmd << " eps:" << epsname;
      if(tgifformat) cmd << " -transparent white gif";
      else cmd << " " << outputformat;
      cmd << ":" << outname;
    }
    System(cmd);
    if(!keep) unlink(epsname.c_str());
  }
  
  if(verbose > (tgifformat ? 1 : 0)) cout << "Wrote " << outname << endl;
  if(view && !deconstruct) {
    if(epsformat || pdfformat) {
      static int pid=0;
      static string lastoutname;
      static const string PSViewers[]={PSViewer,"gv","ggv","ghostview",
				       "kghostview","gsview"};
      static const string PDFViewers[]={PDFViewer,"gv","acroread","xpdf"};
      static const size_t nPSViewers=sizeof(PSViewers)/sizeof(string);
      static const size_t nPDFViewers=sizeof(PDFViewers)/sizeof(string);
      const string *Viewers=pdfformat ? PDFViewers : PSViewers;
      const size_t nViewers=pdfformat ? nPDFViewers : nPSViewers;
      size_t iViewer=0;
      int status;
      bool restart=false;
      if(interact::interactive && pid)
	restart=(waitpid(pid, &status, WNOHANG) == pid);

      if (!interact::virtualEOF || outname != lastoutname || restart) {
	if(!wait) lastoutname=outname;
	status=-1;
	while(status == -1 && iViewer < nViewers) {
	  if(iViewer == 1 && Viewers[0] == Viewers[1]) {
	    iViewer++;
	    continue;
	  }
	  ostringstream cmd;
	  cmd << Viewers[iViewer];
	  if(Viewers[iViewer] == "gv" && interact::virtualEOF)
	    cmd << " -nowatch";
	  cmd << " " << outname;
	  status=System(cmd,false,wait,&pid,iViewer+1 == nViewers);
	  if(status == -1) ++iViewer;
	}
	if(status) return false;
	// Tell gv it should reread the file.
      } else if(Viewers[iViewer] == "gv") kill(pid,SIGHUP);
    } else {
      ostringstream cmd;
      cmd << "display " << outname;
      status=System(cmd,false,wait);
      if(status) return false;
    }
  }
  
  return true;
}

bool picture::shipout(const picture& preamble, const string& prefix,
		      const string& format, bool wait)
{
  if(settings::suppressStandard) return true;
  
  checkFormatString(format);
  string outputformat=format == "" ? outformat : format;
  epsformat=outputformat == "" || outputformat == "eps";
  pdfformat=outputformat == "pdf";
  tgifformat=outputformat == "tgif";
  string outname=tgifformat ? "."+buildname(prefix,"gif") :
    buildname(prefix,outputformat);
  string epsname=epsformat ? outname : auxname(prefix,"eps");
  
  bounds();
  
  if(b.right <= b.left && b.top <= b.bottom) { // null picture
    unlink(outname.c_str());
    return true;
  }
  
  static std::ofstream bboxout;
  if(deconstruct && !tgifformat) {
    if(bboxout) bboxout.close();
    if(view) {
      ostringstream cmd;
      cmd << "xasy " << buildname(prefix) 
	  << " " << ShipoutNumber << " " << buildname(settings::outname);
      System(cmd,false,true);
    }
    ShipoutNumber++;
    return true;
  }
      
  bbox bpos=b;
  
  if(!labels && pdfformat) {
    double fuzz=1.0;
    bpos.left -= fuzz;
    bpos.right += fuzz;
    bpos.bottom -= fuzz;
    bpos.top += fuzz;
  }
  
  if(deconstruct) {
      if(!bboxout.is_open()) {
	bboxout.open(("."+buildname(prefix,"box")).c_str());	
	bboxout << deconstruct << endl;
      }
      bbox bscaled=b;
      bscaled *= deconstruct;
      bboxout << bscaled << endl;
  }
  
  // Avoid negative bounding box coordinates
  bboxshift=origin == ZERO ? 0.0 : pair(-bpos.left,-bpos.bottom);
  if(!pdfformat) {
    bboxshift += postscriptOffset;
    if(!(origin == BOTTOM || origin == ZERO)) {
      double yexcess=max(pageHeight-(bpos.top-bpos.bottom),0.0);
      if(origin == TOP) bboxshift += pair(0.5,yexcess-0.5);
      else {
	double xexcess=max(pageWidth-(bpos.right-bpos.left),0.0);
	bboxshift += 0.5*pair(xexcess,yexcess);
      }
    }
  }
  bpos.shift(bboxshift);
  
  string texname=auxname(prefix,"tex");
  texfile *tex=NULL;
  bool status = true;
  
  if(labels) {
    tex=new texfile(texname,b);
    list<drawElement*>::iterator p;
    for (p = nodes.begin(); p != nodes.end(); ++p) {
      assert(*p);
      if (!(*p)->setup(tex))
	status = false;
    }
  
    tex->prologue();
  }
  
  list<drawElement*>::iterator layerp=nodes.begin();
  list<drawElement*>::iterator p=layerp;
  unsigned int layer=0;
  list<string> psnameStack;
  
  while(p != nodes.end()) {
    ostringstream buf;
    buf << prefix << "_" << layer;
    string psname=labels ? buildname(buf.str(),"ps") : epsname;
    psnameStack.push_back(psname);
    psfile out(psname,bpos,bboxshift);
    out.prologue();
  
    if(labels) tex->beginlayer(psname);
  
    // Postscript preamble.
    std::list<drawElement*> Nodes=preamble.nodes;
    list<drawElement*>::iterator P=Nodes.begin();
    if(P != Nodes.end()) {
      out.resetpen();
      for(; P != Nodes.end(); ++P) {
	assert(*P);
	out.raw(true);
	if(!(*P)->draw(&out))
	  status = false;
	out.raw(false);
      }
    }
    
    out.resetpen();
    
    for(; p != nodes.end(); ++p) {
      assert(*p);
      if(labels && (*p)->islayer()) break;
      if(!(*p)->draw(&out))
	status = false;
    }
    out.epilogue();
  
    if(status && labels) {
      for (p=layerp; p != nodes.end(); ++p) {
	if((*p)->islayer()) {
	  tex->endlayer();
	  layerp=++p;
	  layer++;
	  break;
	}
	assert(*p);
	if (!(*p)->write(tex))
	  status = false;
      }
    }    
  }
  
  if(status) {
    if(labels) {
      tex->epilogue();
      status=texprocess(texname,epsname,prefix,bpos);
      if(!keep) {
	list<string>::iterator p;
	for(p=psnameStack.begin(); p != psnameStack.end(); ++p)
	  unlink(p->c_str());
      }
    }
    if(status) status=postprocess(epsname,outname,outputformat,wait,bpos);
  }
  
  if(!status) reportError("shipout failed");
    
  if(labels) delete tex;
  
  if(!tgifformat) outnameStack->push_back(outname);
  
  return status;
}

picture *picture::transformed(const transform& t)
{
  picture *pic = new picture;

  list<drawElement*>::iterator p;
  for (p = nodes.begin(); p != nodes.end(); ++p) {
    assert(*p);
    pic->append((*p)->transformed(t));
  }

  return pic;
}


} // namespace camp
