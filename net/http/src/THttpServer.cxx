// $Id$
// Author: Sergey Linev   21/12/2013

#include "THttpServer.h"

#include "TThread.h"
#include "TTimer.h"
#include "TSystem.h"
#include "TImage.h"
#include "TROOT.h"
#include "TClass.h"
#include "TFolder.h"
#include "RVersion.h"
#include "RConfigure.h"

#include "THttpEngine.h"
#include "TRootSniffer.h"
#include "TRootSnifferStore.h"

#include <string>
#include <cstdlib>
#include <stdlib.h>
#include <string.h>
#include <fstream>


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// THttpTimer                                                           //
//                                                                      //
// Specialized timer for THttpServer                                    //
// Provides regular call of THttpServer::ProcessRequests() method       //
//                                                                      //
//////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////

class THttpTimer : public TTimer {
public:

   THttpServer *fServer;  //!

   THttpTimer(Long_t milliSec, Bool_t mode, THttpServer *serv) :
      TTimer(milliSec, mode), fServer(serv)
   {
      // construtor
   }
   virtual ~THttpTimer()
   {
      // destructor
   }
   virtual void Timeout()
   {
      // timeout handler
      // used to process http requests in main ROOT thread

      if (fServer) fServer->ProcessRequests();
   }
};

// =======================================================

//////////////////////////////////////////////////////////////////////////
//                                                                      //
// THttpServer                                                          //
//                                                                      //
// Online http server for arbitrary ROOT application                    //
//                                                                      //
// Idea of THttpServer - provide remote http access to running          //
// ROOT application and enable HTML/JavaScript user interface.          //
// Any registered object can be requested and displayed in the browser. //
// There are many benefits of such approach:                            //
//     * standard http interface to ROOT application                    //
//     * no any temporary ROOT files when access data                   //
//     * user interface running in all browsers                         //
//                                                                      //
// Starting HTTP server                                                 //
//                                                                      //
// To start http server, at any time  create instance                   //
// of the THttpServer class like:                                       //
//    serv = new THttpServer("http:8080");                              //
//                                                                      //
// This will starts civetweb-based http server with http port 8080.     //
// Than one should be able to open address "http://localhost:8080"      //
// in any modern browser (IE, Firefox, Chrome) and browse objects,      //
// created in application. By default, server can access files,         //
// canvases and histograms via gROOT pointer. All such objects          //
// can be displayed with JSROOT graphics.                               //
//                                                                      //
// At any time one could register other objects with the command:       //
//                                                                      //
// TGraph* gr = new TGraph(10);                                         //
// gr->SetName("gr1");                                                  //
// serv->Register("graphs/subfolder", gr);                              //
//                                                                      //
// If objects content is changing in the application, one could         //
// enable monitoring flag in the browser - than objects view            //
// will be regularly updated.                                           //
//                                                                      //
// More information: http://root.cern.ch/drupal/content/users-guide     //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

ClassImp(THttpServer)

////////////////////////////////////////////////////////////////////////////////
/// constructor

THttpServer::THttpServer(const char *engine) :
   TNamed("http", "ROOT http server"),
   fEngines(),
   fTimer(0),
   fSniffer(0),
   fMainThrdId(0),
   fJSROOTSYS(),
   fTopName("ROOT"),
   fJSROOT(),
   fLocations(),
   fDefaultPage(),
   fDefaultPageCont(),
   fDrawPage(),
   fDrawPageCont(),
   fMutex(),
   fCallArgs()
{
   // As argument, one specifies engine kind which should be
   // created like "http:8080". One could specify several engines
   // at once, separating them with ; like "http:8080;fastcgi:9000"
   // One also can configure readonly flag for sniffer like
   // "http:8080;readonly" or "http:8080;readwrite"
   //
   // Also searches for JavaScript ROOT sources, which are used in web clients
   // Typically JSROOT sources located in $ROOTSYS/etc/http directory,
   // but one could set JSROOTSYS variable to specify alternative location

   fLocations.SetOwner(kTRUE);

   // Info("THttpServer", "Create %p in thrd %ld", this, (long) fMainThrdId);

#ifdef COMPILED_WITH_DABC
   const char *dabcsys = gSystem->Getenv("DABCSYS");
   if (dabcsys != 0)
      fJSROOTSYS = TString::Format("%s/plugins/root/js", dabcsys);
#endif

   const char *jsrootsys = gSystem->Getenv("JSROOTSYS");
   if (jsrootsys != 0) fJSROOTSYS = jsrootsys;

   if (fJSROOTSYS.Length() == 0) {
#ifdef ROOTETCDIR
      TString jsdir = TString::Format("%s/http", ROOTETCDIR);
#else
      TString jsdir("$(ROOTSYS)/etc/http");
#endif
      if (gSystem->ExpandPathName(jsdir)) {
         Warning("THttpServer", "problems resolving '%s', use JSROOTSYS to specify $ROOTSYS/etc/http location", jsdir.Data());
         fJSROOTSYS = ".";
      } else {
         fJSROOTSYS = jsdir;
      }
   }

   AddLocation("currentdir/", ".");
   AddLocation("jsrootsys/", fJSROOTSYS);

   const char *rootsys = gSystem->Getenv("ROOTSYS");
   if (rootsys != 0) {
      AddLocation("rootsys/", rootsys);
   } else {
#ifdef ROOTPREFIX
      TString sysdir = ROOTPREFIX;
#else
      TString sysdir = "$(ROOTSYS)";
#endif
      if (!gSystem->ExpandPathName(sysdir)) AddLocation("rootsys/", sysdir);
   }

   fDefaultPage = fJSROOTSYS + "/files/online.htm";
   fDrawPage = fJSROOTSYS + "/files/draw.htm";

   SetSniffer(new TRootSniffer("sniff"));

   // start timer
   SetTimer(20, kTRUE);

   if (strchr(engine, ';') == 0) {
      CreateEngine(engine);
   } else {
      TObjArray *lst = TString(engine).Tokenize(";");

      for (Int_t n = 0; n <= lst->GetLast(); n++) {
         const char *opt = lst->At(n)->GetName();
         if ((strcmp(opt, "readonly") == 0) || (strcmp(opt, "ro") == 0)) {
            GetSniffer()->SetReadOnly(kTRUE);
         } else if ((strcmp(opt, "readwrite") == 0) || (strcmp(opt, "rw") == 0)) {
            GetSniffer()->SetReadOnly(kFALSE);
         } else
            CreateEngine(opt);
      }

      delete lst;
   }
}

////////////////////////////////////////////////////////////////////////////////
/// destructor
/// delete all http engines and sniffer

THttpServer::~THttpServer()
{
   fEngines.Delete();

   SetSniffer(0);

   SetTimer(0);
}

////////////////////////////////////////////////////////////////////////////////
/// Set TRootSniffer to the server
/// Server takes ownership over sniffer

void THttpServer::SetSniffer(TRootSniffer *sniff)
{
   if (fSniffer) delete fSniffer;
   fSniffer = sniff;
}

////////////////////////////////////////////////////////////////////////////////
/// returns read-only mode

Bool_t THttpServer::IsReadOnly() const
{
   return fSniffer ? fSniffer->IsReadOnly() : kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Set read-only mode for the server (default on)
/// In read-only server is not allowed to change any ROOT object, registered to the server
/// Server also cannot execute objects method via exe.json request

void THttpServer::SetReadOnly(Bool_t readonly)
{
   if (fSniffer) fSniffer->SetReadOnly(readonly);
}

////////////////////////////////////////////////////////////////////////////////
/// add files location, which could be used in the server
/// one could map some system folder to the server like AddLocation("mydir/","/home/user/specials");
/// Than files from this directory could be addressed via server like
/// http://localhost:8080/mydir/myfile.root

void THttpServer::AddLocation(const char *prefix, const char *path)
{
   if ((prefix==0) || (*prefix==0)) return;

   TNamed *obj = dynamic_cast<TNamed*> (fLocations.FindObject(prefix));
   if (obj != 0) {
      obj->SetTitle(path);
   } else {
      fLocations.Add(new TNamed(prefix, path));
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Set location of JSROOT to use with the server
/// One could specify address like:
///   https://root.cern.ch/js/3.3/
///   http://web-docs.gsi.de/~linev/js/3.3/
/// This allows to get new JSROOT features with old server,
/// reduce load on THttpServer instance, also startup time can be improved
/// When empty string specified (default), local copy of JSROOT is used (distributed with ROOT)

void THttpServer::SetJSROOT(const char* location)
{
   fJSROOT = location ? location : "";
}

////////////////////////////////////////////////////////////////////////////////
/// Set file name of HTML page, delivered by the server when
/// http address is opened in the browser.
/// By default, $ROOTSYS/etc/http/files/online.htm page is used
/// When empty filename is specified, default page will be used

void THttpServer::SetDefaultPage(const char* filename)
{
   if ((filename!=0) && (*filename!=0))
      fDefaultPage = filename;
   else
      fDefaultPage = fJSROOTSYS + "/files/online.htm";

   // force to read page content next time again
   fDefaultPageCont.Clear();
}

////////////////////////////////////////////////////////////////////////////////
/// Set file name of HTML page, delivered by the server when
/// objects drawing page is requested from the browser
/// By default, $ROOTSYS/etc/http/files/draw.htm page is used
/// When empty filename is specified, default page will be used

void THttpServer::SetDrawPage(const char* filename)
{
   if ((filename!=0) && (*filename!=0))
      fDrawPage = filename;
   else
      fDrawPage = fJSROOTSYS + "/files/draw.htm";

   // force to read page content next time again
   fDrawPageCont.Clear();
}

////////////////////////////////////////////////////////////////////////////////
/// factory method to create different http engines
/// At the moment two engine kinds are supported:
///  civetweb (default) and fastcgi
/// Examples:
///   "civetweb:8080" or "http:8080" or ":8080" - creates civetweb web server with http port 8080
///   "fastcgi:9000" - creates fastcgi server with port 9000
///   "dabc:1237"    - create DABC server with port 1237 (only available with DABC installed)
///   "dabc:master_host:port" - attach to DABC master, running on master_host:port (only available with DABC installed)

Bool_t THttpServer::CreateEngine(const char *engine)
{
   if (engine == 0) return kFALSE;

   const char *arg = strchr(engine, ':');
   if (arg == 0) return kFALSE;

   TString clname;
   if (arg != engine) clname.Append(engine, arg - engine);

   if ((clname.Length() == 0) || (clname == "http") || (clname == "civetweb"))
      clname = "TCivetweb";
   else if (clname == "fastcgi")
      clname = "TFastCgi";
   else if (clname == "dabc")
      clname = "TDabcEngine";

   // ensure that required engine class exists before we try to create it
   TClass *engine_class = gROOT->LoadClass(clname.Data());
   if (engine_class == 0) return kFALSE;

   THttpEngine *eng = (THttpEngine *) engine_class->New();
   if (eng == 0) return kFALSE;

   eng->SetServer(this);

   if (!eng->Create(arg + 1)) {
      delete eng;
      return kFALSE;
   }

   fEngines.Add(eng);

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// create timer which will invoke ProcessRequests() function periodically
/// Timer is required to perform all actions in main ROOT thread
/// Method arguments are the same as for TTimer constructor
/// By default, sync timer with 100 ms period is created
///
/// If milliSec == 0, no timer will be created.
/// In this case application should regularly call ProcessRequests() method.

void THttpServer::SetTimer(Long_t milliSec, Bool_t mode)
{
   if (fTimer) {
      fTimer->Stop();
      delete fTimer;
      fTimer = 0;
   }
   if (milliSec > 0) {
      fTimer = new THttpTimer(milliSec, mode, this);
      fTimer->TurnOn();
   }
}

////////////////////////////////////////////////////////////////////////////////
/// Checked that filename does not contains relative path below current directory
/// Used to prevent access to files below current directory

Bool_t THttpServer::VerifyFilePath(const char *fname)
{
   if ((fname == 0) || (*fname == 0)) return kFALSE;

   Int_t level = 0;

   while (*fname != 0) {

      // find next slash or backslash
      const char *next = strpbrk(fname, "/\\");
      if (next == 0) return kTRUE;

      // most important - change to parent dir
      if ((next == fname + 2) && (*fname == '.') && (*(fname + 1) == '.')) {
         fname += 3;
         level--;
         if (level < 0) return kFALSE;
         continue;
      }

      // ignore current directory
      if ((next == fname + 1) && (*fname == '.'))  {
         fname += 2;
         continue;
      }

      // ignore slash at the front
      if (next == fname) {
         fname ++;
         continue;
      }

      fname = next + 1;
      level++;
   }

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Verifies that request is just file name
/// File names typically contains prefix like "jsrootsys/"
/// If true, method returns real name of the file,
/// which should be delivered to the client
/// Method is thread safe and can be called from any thread

Bool_t THttpServer::IsFileRequested(const char *uri, TString &res) const
{
   if ((uri == 0) || (strlen(uri) == 0)) return kFALSE;

   TString fname = uri;

   TIter iter(&fLocations);
   TObject *obj(0);
   while ((obj=iter()) != 0) {
      Ssiz_t pos = fname.Index(obj->GetName());
      if (pos == kNPOS) continue;
      fname.Remove(0, pos + (strlen(obj->GetName()) - 1));
      if (!VerifyFilePath(fname.Data())) return kFALSE;
      res = obj->GetTitle();
      if ((fname[0]=='/') && (res[res.Length()-1]=='/')) res.Resize(res.Length()-1);
      res.Append(fname);
      return kTRUE;
   }

   return kFALSE;
}

////////////////////////////////////////////////////////////////////////////////
/// Executes http request, specified in THttpCallArg structure
/// Method can be called from any thread
/// Actual execution will be done in main ROOT thread, where analysis code is running.

Bool_t THttpServer::ExecuteHttp(THttpCallArg *arg)
{
   if ((fMainThrdId!=0) && (fMainThrdId == TThread::SelfId())) {
      // should not happen, but one could process requests directly without any signaling

      ProcessRequest(arg);

      return kTRUE;
   }

   // add call arg to the list
   fMutex.Lock();
   fCallArgs.Add(arg);
   fMutex.UnLock();

   // and now wait until request is processed
   arg->fCond.Wait();

   return kTRUE;
}

////////////////////////////////////////////////////////////////////////////////
/// Process requests, submitted for execution
/// Regularly invoked by THttpTimer, when somewhere in the code
/// gSystem->ProcessEvents() is called.
/// User can call serv->ProcessRequests() directly, but only from main analysis thread.

void THttpServer::ProcessRequests()
{
   if (fMainThrdId==0) fMainThrdId = TThread::SelfId();

   if (fMainThrdId != TThread::SelfId()) {
      Error("ProcessRequests", "Should be called only from main ROOT thread");
      return;
   }

   while (true) {
      THttpCallArg *arg = 0;

      fMutex.Lock();
      if (fCallArgs.GetSize() > 0) {
         arg = (THttpCallArg *) fCallArgs.First();
         fCallArgs.RemoveFirst();
      }
      fMutex.UnLock();

      if (arg == 0) break;

      fSniffer->SetCurrentCallArg(arg);

      try {
         ProcessRequest(arg);
         fSniffer->SetCurrentCallArg(0);
      } catch (...) {
         fSniffer->SetCurrentCallArg(0);
      }

      arg->fCond.Signal();
   }

   // regularly call Process() method of engine to let perform actions in ROOT context
   TIter iter(&fEngines);
   THttpEngine *engine = 0;
   while ((engine = (THttpEngine *)iter()) != 0)
      engine->Process();
}

////////////////////////////////////////////////////////////////////////////////
/// Process single http request
/// Depending from requested path and filename different actions will be performed.
/// In most cases information is provided by TRootSniffer class

void THttpServer::ProcessRequest(THttpCallArg *arg)
{

   if (arg->fFileName.IsNull() || (arg->fFileName == "index.htm")) {

      if (fDefaultPageCont.Length() == 0) {
         Int_t len = 0;
         char *buf = ReadFileContent(fDefaultPage.Data(), len);
         if (len > 0) fDefaultPageCont.Append(buf, len);
         delete buf;
      }

      if (fDefaultPageCont.Length() == 0) {
         arg->Set404();
      } else {
         arg->fContent = fDefaultPageCont;

         // replace all references on JSROOT
         if (fJSROOT.Length() > 0) {
            TString repl = TString("=\"") + fJSROOT;
            if (!repl.EndsWith("/")) repl+="/";
            arg->fContent.ReplaceAll("=\"jsrootsys/", repl);
         }

         const char *hjsontag = "\"$$$h.json$$$\"";

         // add h.json caching
         if (arg->fContent.Index(hjsontag) != kNPOS) {
            TString h_json;
            TRootSnifferStoreJson store(h_json, kTRUE);
            const char *topname = fTopName.Data();
            if (arg->fTopName.Length() > 0) topname = arg->fTopName.Data();
            fSniffer->ScanHierarchy(topname, arg->fPathName.Data(), &store);

            arg->fContent.ReplaceAll(hjsontag, h_json);

            arg->AddHeader("Cache-Control", "private, no-cache, no-store, must-revalidate, max-age=0, proxy-revalidate, s-maxage=0");
            if (arg->fQuery.Index("nozip") == kNPOS) arg->SetZipping(2);
         }
         arg->SetContentType("text/html");
      }
      return;
   }

   if (arg->fFileName == "draw.htm") {
      if (fDrawPageCont.Length() == 0) {
         Int_t len = 0;
         char *buf = ReadFileContent(fDrawPage.Data(), len);
         if (len > 0) fDrawPageCont.Append(buf, len);
         delete buf;
      }

      if (fDrawPageCont.Length() == 0) {
         arg->Set404();
      } else {
         const char *rootjsontag = "\"$$$root.json$$$\"";
         const char *hjsontag = "\"$$$h.json$$$\"";

         arg->fContent = fDrawPageCont;

         // replace all references on JSROOT
         if (fJSROOT.Length() > 0) {
            TString repl = TString("=\"") + fJSROOT;
            if (!repl.EndsWith("/")) repl+="/";
            arg->fContent.ReplaceAll("=\"jsrootsys/", repl);
         }

         if (arg->fContent.Index(hjsontag) != kNPOS) {
            TString h_json;
            TRootSnifferStoreJson store(h_json, kTRUE);
            const char *topname = fTopName.Data();
            if (arg->fTopName.Length() > 0) topname = arg->fTopName.Data();
            fSniffer->ScanHierarchy(topname, arg->fPathName.Data(), &store, kTRUE);

            arg->fContent.ReplaceAll(hjsontag, h_json);
         }

         if (arg->fContent.Index(rootjsontag) != kNPOS) {
            TString str;
            void *bindata = 0;
            Long_t bindatalen = 0;
            if (fSniffer->Produce(arg->fPathName.Data(), "root.json", "compact=3", bindata, bindatalen, str)) {
               arg->fContent.ReplaceAll(rootjsontag, str);
            }
         }
         arg->AddHeader("Cache-Control", "private, no-cache, no-store, must-revalidate, max-age=0, proxy-revalidate, s-maxage=0");
         if (arg->fQuery.Index("nozip") == kNPOS) arg->SetZipping(2);
         arg->SetContentType("text/html");
      }
      return;
   }

   TString filename;
   if (IsFileRequested(arg->fFileName.Data(), filename)) {
      arg->SetFile(filename);
      return;
   }

   filename = arg->fFileName;
   Bool_t iszip = kFALSE;
   if (filename.EndsWith(".gz")) {
      filename.Resize(filename.Length() - 3);
      iszip = kTRUE;
   }

   void* bindata(0);
   Long_t bindatalen(0);

   if ((filename == "h.xml") || (filename == "get.xml"))  {

      Bool_t compact = arg->fQuery.Index("compact") != kNPOS;

      arg->fContent.Form("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
      if (!compact) arg->fContent.Append("\n");
      arg->fContent.Append("<root>");
      if (!compact) arg->fContent.Append("\n");
      {
         TRootSnifferStoreXml store(arg->fContent, compact);

         const char *topname = fTopName.Data();
         if (arg->fTopName.Length() > 0) topname = arg->fTopName.Data();
         fSniffer->ScanHierarchy(topname, arg->fPathName.Data(), &store, filename == "get.xml");
      }

      arg->fContent.Append("</root>");
      if (!compact) arg->fContent.Append("\n");

      arg->SetXml();
   } else

   if (filename == "h.json")  {
      TRootSnifferStoreJson store(arg->fContent, arg->fQuery.Index("compact") != kNPOS);
      const char *topname = fTopName.Data();
      if (arg->fTopName.Length() > 0) topname = arg->fTopName.Data();
      fSniffer->ScanHierarchy(topname, arg->fPathName.Data(), &store);
      arg->SetJson();
   } else

   if (fSniffer->Produce(arg->fPathName.Data(), filename.Data(), arg->fQuery.Data(), bindata, bindatalen, arg->fContent)) {
      if (bindata != 0) arg->SetBinData(bindata, bindatalen);

      // define content type base on extension
      arg->SetContentType(GetMimeType(filename.Data()));
   } else {
      // request is not processed
      arg->Set404();
   }

   if (arg->Is404()) return;

   if (iszip) arg->SetZipping(3);

   if (filename == "root.bin") {
      // only for binary data master version is important
      // it allows to detect if streamer info was modified
      const char *parname = fSniffer->IsStreamerInfoItem(arg->fPathName.Data()) ? "BVersion" : "MVersion";
      arg->AddHeader(parname, Form("%u", (unsigned) fSniffer->GetStreamerInfoHash()));
   }

   // try to avoid caching on the browser
   arg->AddHeader("Cache-Control", "private, no-cache, no-store, must-revalidate, max-age=0, proxy-revalidate, s-maxage=0");
}

////////////////////////////////////////////////////////////////////////////////
/// Register object in folders hierarchy
///
/// See TRootSniffer::RegisterObject() for more details

Bool_t THttpServer::Register(const char *subfolder, TObject *obj)
{
   return fSniffer->RegisterObject(subfolder, obj);
}

////////////////////////////////////////////////////////////////////////////////
/// Unregister object in folders hierarchy
///
/// See TRootSniffer::UnregisterObject() for more details

Bool_t THttpServer::Unregister(TObject *obj)
{
   return fSniffer->UnregisterObject(obj);
}

////////////////////////////////////////////////////////////////////////////////
/// Restrict access to specified object
///
/// See TRootSniffer::Restrict() for more details

void THttpServer::Restrict(const char *path, const char* options)
{
   fSniffer->Restrict(path, options);
}

////////////////////////////////////////////////////////////////////////////////
/// Register command which can be executed from web interface
///
/// As method one typically specifies string, which is executed with
/// gROOT->ProcessLine() method. For instance
///    serv->RegisterCommand("Invoke","InvokeFunction()");
///
/// Or one could specify any method of the object which is already registered
/// to the server. For instance:
///     serv->Register("/", hpx);
///     serv->RegisterCommand("/ResetHPX", "/hpx/->Reset()");
/// Here symbols '/->' separates item name from method to be executed
///
/// One could specify additional arguments in the command with
/// syntax like %arg1%, %arg2% and so on. For example:
///     serv->RegisterCommand("/ResetHPX", "/hpx/->SetTitle(\"%arg1%\")");
///     serv->RegisterCommand("/RebinHPXPY", "/hpxpy/->Rebin2D(%arg1%,%arg2%)");
/// Such parameter(s) will be requested when command clicked in the browser.
///
/// Once command is registered, one could specify icon which will appear in the browser:
///     serv->SetIcon("/ResetHPX", "rootsys/icons/ed_execute.png");
///
/// One also can set extra property '_fastcmd', that command appear as
/// tool button on the top of the browser tree:
///     serv->SetItemField("/ResetHPX", "_fastcmd", "true");
/// Or it is equivalent to specifying extra argument when register command:
///     serv->RegisterCommand("/ResetHPX", "/hpx/->Reset()", "button;rootsys/icons/ed_delete.png");

Bool_t THttpServer::RegisterCommand(const char *cmdname, const char *method, const char *icon)
{
   return fSniffer->RegisterCommand(cmdname, method, icon);
}

////////////////////////////////////////////////////////////////////////////////
/// hides folder or element from web gui

Bool_t THttpServer::Hide(const char *foldername, Bool_t hide)
{
   return SetItemField(foldername, "_hidden", hide ? "true" : (const char *) 0);
}

////////////////////////////////////////////////////////////////////////////////
/// set name of icon, used in browser together with the item
///
/// One could use images from $ROOTSYS directory like:
///    serv->SetIcon("/ResetHPX","/rootsys/icons/ed_execute.png");

Bool_t THttpServer::SetIcon(const char *fullname, const char *iconname)
{
   return SetItemField(fullname, "_icon", iconname);
}

////////////////////////////////////////////////////////////////////////////////

Bool_t THttpServer::CreateItem(const char *fullname, const char *title)
{
   return fSniffer->CreateItem(fullname, title);
}

////////////////////////////////////////////////////////////////////////////////

Bool_t THttpServer::SetItemField(const char *fullname, const char *name, const char *value)
{
   return fSniffer->SetItemField(fullname, name, value);
}

////////////////////////////////////////////////////////////////////////////////

const char *THttpServer::GetItemField(const char *fullname, const char *name)
{
   return fSniffer->GetItemField(fullname, name);
}

////////////////////////////////////////////////////////////////////////////////
/// Returns MIME type base on file extension

const char *THttpServer::GetMimeType(const char *path)
{
   static const struct {
      const char *extension;
      int ext_len;
      const char *mime_type;
   } builtin_mime_types[] = {
      {".xml", 4, "text/xml"},
      {".json", 5, "application/json"},
      {".bin", 4, "application/x-binary"},
      {".gif", 4, "image/gif"},
      {".jpg", 4, "image/jpeg"},
      {".png", 4, "image/png"},
      {".html", 5, "text/html"},
      {".htm", 4, "text/html"},
      {".shtm", 5, "text/html"},
      {".shtml", 6, "text/html"},
      {".css", 4, "text/css"},
      {".js",  3, "application/x-javascript"},
      {".ico", 4, "image/x-icon"},
      {".jpeg", 5, "image/jpeg"},
      {".svg", 4, "image/svg+xml"},
      {".txt", 4, "text/plain"},
      {".torrent", 8, "application/x-bittorrent"},
      {".wav", 4, "audio/x-wav"},
      {".mp3", 4, "audio/x-mp3"},
      {".mid", 4, "audio/mid"},
      {".m3u", 4, "audio/x-mpegurl"},
      {".ogg", 4, "application/ogg"},
      {".ram", 4, "audio/x-pn-realaudio"},
      {".xslt", 5, "application/xml"},
      {".xsl", 4, "application/xml"},
      {".ra",  3, "audio/x-pn-realaudio"},
      {".doc", 4, "application/msword"},
      {".exe", 4, "application/octet-stream"},
      {".zip", 4, "application/x-zip-compressed"},
      {".xls", 4, "application/excel"},
      {".tgz", 4, "application/x-tar-gz"},
      {".tar", 4, "application/x-tar"},
      {".gz",  3, "application/x-gunzip"},
      {".arj", 4, "application/x-arj-compressed"},
      {".rar", 4, "application/x-arj-compressed"},
      {".rtf", 4, "application/rtf"},
      {".pdf", 4, "application/pdf"},
      {".swf", 4, "application/x-shockwave-flash"},
      {".mpg", 4, "video/mpeg"},
      {".webm", 5, "video/webm"},
      {".mpeg", 5, "video/mpeg"},
      {".mov", 4, "video/quicktime"},
      {".mp4", 4, "video/mp4"},
      {".m4v", 4, "video/x-m4v"},
      {".asf", 4, "video/x-ms-asf"},
      {".avi", 4, "video/x-msvideo"},
      {".bmp", 4, "image/bmp"},
      {".ttf", 4, "application/x-font-ttf"},
      {NULL,  0, NULL}
   };

   int path_len = strlen(path);

   for (int i = 0; builtin_mime_types[i].extension != NULL; i++) {
      if (path_len <= builtin_mime_types[i].ext_len) continue;
      const char *ext = path + (path_len - builtin_mime_types[i].ext_len);
      if (strcmp(ext, builtin_mime_types[i].extension) == 0) {
         return builtin_mime_types[i].mime_type;
      }
   }

   return "text/plain";
}

////////////////////////////////////////////////////////////////////////////////
/// reads file content

char *THttpServer::ReadFileContent(const char *filename, Int_t &len)
{
   len = 0;

   std::ifstream is(filename);
   if (!is) return 0;

   is.seekg(0, is.end);
   len = is.tellg();
   is.seekg(0, is.beg);

   char *buf = (char *) malloc(len);
   is.read(buf, len);
   if (!is) {
      free(buf);
      len = 0;
      return 0;
   }

   return buf;
}
