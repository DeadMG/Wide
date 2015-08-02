using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Linq;
using System.Threading;
using System.Windows.Media;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Text.Editor;
using Microsoft.VisualStudio.Text.Operations;
using Microsoft.VisualStudio.Text.Tagging;
using Microsoft.VisualStudio.Utilities;
using Microsoft.VisualStudio.Shell.Interop;
using Microsoft.VisualStudio.Shell;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio.VCProjectEngine;
using Microsoft.VisualStudio.ComponentModelHost;

namespace VisualWide
{
    public class ProjectUtils
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ErrorCallback(int count, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0)]ParserProvider.CCombinedError[] what, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void GetCombinerErrors(System.IntPtr combiner, ErrorCallback callback, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr CreateCombiner();

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void AddParser(System.IntPtr combiner, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 2)]System.IntPtr[] parsers, int count);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr CreateClangOptions([MarshalAs(UnmanagedType.LPStr)]System.String triple);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DestroyClangOptions(System.IntPtr opts);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void AddHeaderPath(System.IntPtr opts, [MarshalAs(UnmanagedType.LPStr)]System.String path, bool angled);
        
        public enum ContextType {
            Unknown,
            Module,
            Type,
            OverloadSet
        };

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void AnalyzerErrorCallback(LexerProvider.CRange where, System.IntPtr what, System.IntPtr context);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void AnalyzerQuickInfoCallback(LexerProvider.CRange where, System.IntPtr what, ContextType type, System.IntPtr context);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void AnalyzerParameterCallback(LexerProvider.CRange where, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void AnalyzeWide(System.IntPtr combiner, System.IntPtr opts, AnalyzerErrorCallback error, AnalyzerQuickInfoCallback quickinfo, AnalyzerParameterCallback param, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr GetAnalyzerErrorString(System.IntPtr err);

        private Dictionary<EnvDTE.Project, Project> project_maps = new Dictionary<EnvDTE.Project, Project>();

        IEnumerable<Project> Projects
        {
            get
            {
                return project_maps.Select(pair => pair.Value);
            }
        }

        static HashSet<String> GetFiles(EnvDTE.ProjectItems items)
        {
            if (items == null)
                return null;

            HashSet<String> results = new HashSet<string>();

            foreach (EnvDTE.ProjectItem item in items)
            {
                if (item.ProjectItems != null)
                    foreach (var subname in GetFiles(item.ProjectItems))
                        results.Add(subname);

                if (item.FileCount <= 0)
                    continue;
                var name = item.FileNames[1];
                if (!name.EndsWith(".wide"))
                    continue;
                results.Add(name);
            }
            return results;
        }

        public class Project
        {
            public class AnalyzerError
            {
                public AnalyzerError(SnapshotSpan snapspan, String err)
                {
                    where = snapspan;
                    error = err;
                }
                public SnapshotSpan where;
                public String error;
            }
            public class AnalyzerInfo
            {
                public AnalyzerInfo(SnapshotSpan snapspan, String inf, ContextType what)
                {
                    where = snapspan;
                    info = inf;
                    type = what;
                }
                public SnapshotSpan where;
                public String info;
                public ContextType type;
            }

            System.IntPtr combiner = CreateCombiner();
            EnvDTE.Project project;

            HashSet<string> FileCache = new HashSet<string>();

            void Update()
            {
                HashSet<System.IntPtr> newparsers = new HashSet<IntPtr>();
                foreach (var parser in FileCache.Select(filename => ParserProvider.GetProviderForBuffer(ProjectUtils.instance.GetBufferForFilename(filename)).GetCurrentParser()))
                {
                    newparsers.Add(parser);
                }
                AddParser(combiner, newparsers.ToArray(), newparsers.Count);
                List<List<ParserProvider.CCombinedError>> CombinedErrors = new List<List<ParserProvider.CCombinedError>>();
                GetCombinerErrors(combiner, (count, array, context) =>
                {
                    CombinedErrors.Add(new List<ParserProvider.CCombinedError>(array));
                }, System.IntPtr.Zero);
                CombinerErrors = CombinedErrors.Select(list => list.Select(cerror =>
                {
                    var name = LexerProvider.RangeFromCRange(cerror.where).begin.location;
                    var buffer = ProjectUtils.instance.GetBufferForFilename(name);
                    return new ParserProvider.CombinedError(cerror.where, buffer.CurrentSnapshot, cerror.what);
                }));

                VCProject vcproj = (VCProject)project.Object;
                var configs = vcproj.Configurations as IVCCollection;
                var config = configs.Item(1) as VCConfiguration;
                var tools = config.Tools as IVCCollection;
                var cltool = tools.Item("VCCLCompilerTool") as VCCLCompilerTool;
                string[] fullname = cltool.FullIncludePath.Split(new char[] { ';' });
                HashSet<String> paths = new HashSet<string>();
                foreach (var _path in fullname)
                {
                    var path = _path;
                    if (System.String.IsNullOrWhiteSpace(path))
                        continue;
                    if (!System.IO.Path.IsPathRooted(path))
                        path = System.IO.Path.Combine(new string[] { System.IO.Path.GetDirectoryName(project.FullName), path });
                    //if (path.Contains("Microsoft") || path.Contains("Windows"))
                    //    continue;
                    paths.Add(path);
                }
                var opts = CreateClangOptions("i686-pc-mingw32");
                foreach (var path in paths)
                    AddHeaderPath(opts, path, true);
                List<AnalyzerError> errors = new List<AnalyzerError>();
                List<AnalyzerInfo> info = new List<AnalyzerInfo>();
                List<SnapshotSpan> param = new List<SnapshotSpan>();
                AnalyzeWide(
                    combiner,
                    opts,
                    (where, what, context) =>
                    {
                        var span = LexerProvider.SpanFromLexer(where);
                        var filename = Marshal.PtrToStringAnsi(where.begin.location);
                        var snapshot = ProjectUtils.instance.GetBufferForFilename(filename).CurrentSnapshot;
                        errors.Add(new AnalyzerError(new SnapshotSpan(snapshot, span), Marshal.PtrToStringAnsi(what)));
                    },
                    (where, what, type, context) =>
                    {
                        var span = LexerProvider.SpanFromLexer(where);
                        var filename = Marshal.PtrToStringAnsi(where.begin.location);
                        var snapshot = ProjectUtils.instance.GetBufferForFilename(filename).CurrentSnapshot;
                        info.Add(new AnalyzerInfo(new SnapshotSpan(snapshot, span), Marshal.PtrToStringAnsi(what), type));
                    },
                    (where, context) => {
                        var span = LexerProvider.SpanFromLexer(where);
                        var filename = Marshal.PtrToStringAnsi(where.begin.location);
                        var snapshot = ProjectUtils.instance.GetBufferForFilename(filename).CurrentSnapshot;
                        param.Add(new SnapshotSpan(snapshot, span));
                    },
                    System.IntPtr.Zero
                );
                AnalyzerQuickInfo = info;
                AnalyzerErrors = errors;
                AnalyzerParameterHighlights = param;
                DestroyClangOptions(opts);
                OnUpdate();
            }

            public delegate void UpdateCallback();
            public event UpdateCallback OnUpdate = delegate { };

            public IEnumerable<IEnumerable<ParserProvider.CombinedError>> CombinerErrors
            {
                get;
                private set;
            }

            public IEnumerable<AnalyzerError> AnalyzerErrors
            {
                get;
                private set;
            }


            public IEnumerable<AnalyzerInfo> AnalyzerQuickInfo
            {
                get;
                private set;
            }

            public IEnumerable<SnapshotSpan> AnalyzerParameterHighlights
            {
                get;
                private set;
            }

            void AddFile(String filepath)
            {
                FileCache.Add(filepath);
                ProjectUtils.instance.GetBufferForFilename(filepath).Changed += (arg, sender) => { Update(); };
            }

            VCProjectEngineEvents events;
            public Project(EnvDTE.Project proj)
            {
                CombinerErrors = new List<List<ParserProvider.CombinedError>>();
                AnalyzerErrors = new List<AnalyzerError>();
                AnalyzerQuickInfo = new List<AnalyzerInfo>();
                AnalyzerParameterHighlights = new List<SnapshotSpan>();

                project = proj;
                var vcproj = (VCProject)proj.Object;
                events = (VCProjectEngineEvents)vcproj.VCProjectEngine.Events;
                FileCache = GetFiles(project.ProjectItems);

                events.ItemAdded += (object item, object parent) =>
                {
                    VCFile file = item as VCFile;
                    if (file != null)
                        if (file.FullPath.EndsWith(".wide"))
                            AddFile(file.FullPath);
                };

                events.ItemRemoved += (object item, object parent) =>
                {
                    VCFile file = item as VCFile;
                    if (file != null)
                        if (file.FullPath.EndsWith(".wide"))
                            FileCache.Remove(file.FullPath);
                };

                events.ItemRenamed += (object item, object parent, String oldname) =>
                {
                    if (oldname != null)
                        if (oldname.EndsWith(".wide"))
                            FileCache.Remove(oldname);
                    VCFile file = item as VCFile;
                    if (file != null)
                        if (file.FullPath.EndsWith(".wide"))
                            AddFile(file.FullPath);
                };

                foreach (var buffer in FileCache.Select(filename => ProjectUtils.instance.GetBufferForFilename(filename)))
                {
                    buffer.Changed += (arg, sender) => {
                        Update();
                    };
                }

                Update();
            }
            public bool ContainsFile(String filepath)
            {
                return FileCache.Contains(filepath);
            }
        }       
        
        private static ProjectUtils hidden;
        public static ProjectUtils instance
        {
            get
            {
                if (hidden == null)
                    hidden = new ProjectUtils();
                return hidden;
            }
        }

        [Import]
        public ITextDocumentFactoryService factory = null;

        [Import(typeof(SVsServiceProvider))]
        public IServiceProvider serviceProvider = null;

        [Import]
        public Microsoft.VisualStudio.Editor.IVsEditorAdaptersFactoryService editorfactory = null;
        
        [Import]
        public IContentTypeRegistryService registryservice = null;

        RunningDocumentTable table;

        public static string GetFileName(ITextDocumentFactoryService factory, ITextBuffer buffer)
        {
            ITextDocument doc;
            if (factory.TryGetTextDocument(buffer, out doc))
                return doc.FilePath;
            return null;
        }

        public string GetFileName(ITextBuffer buffer)
        {
            return GetFileName(factory, buffer);
        }

        public IEnumerable<Project> GetProjectsFor(ITextBuffer buffer)
        {
            foreach (var project in Projects)
                if (project.ContainsFile(GetFileName(buffer)))
                    yield return project;
        }

        Dictionary<string, ITextBuffer> FileBuffers = new Dictionary<string, ITextBuffer>();
        Dictionary<string, ITextBuffer> OpenBuffers = new Dictionary<string, ITextBuffer>();

        private ITextBuffer GetBufferForFilename(string filename)
        {
            if (OpenBuffers.ContainsKey(filename))
                return OpenBuffers[filename];
            if (FileBuffers.ContainsKey(filename))
                return FileBuffers[filename];
            return FileBuffers[filename] = factory.CreateAndLoadTextDocument(filename, registryservice.GetContentType("Wide")).TextBuffer;            
        }

        private ITextBuffer GetOpenBufferForFilename(string filename) {
            uint cookie;
            table.FindDocument(filename, out cookie);
            var info = table.GetDocumentInfo(cookie);
            var lines = info.DocData as Microsoft.VisualStudio.TextManager.Interop.IVsTextLines;
            if (lines == null)
            {
                var provider = info.DocData as IVsTextBufferProvider;
                if (provider != null)
                    provider.GetTextBuffer(out lines);
                if (lines == null)
                    return null;
            }
            return editorfactory.GetDataBuffer(lines);            
        }
                        
        EnvDTE.SolutionEvents solevents;

        public static void SatisfyMEFImports(Object obj)
        {
            (Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(SComponentModel)) as IComponentModel).DefaultCompositionService.SatisfyImportsOnce(obj);
        }

        public ProjectUtils()
        {
            hidden = this;
            SatisfyMEFImports(this);
            table = new RunningDocumentTable(serviceProvider);
            var dte = (EnvDTE.DTE)Microsoft.VisualStudio.Shell.Package.GetGlobalService(typeof(SDTE));
            foreach (EnvDTE.Document doc in dte.Documents)
            {
                OpenBuffers[doc.FullName] = GetOpenBufferForFilename(doc.FullName);
            }
            foreach (EnvDTE.Project proj in dte.Solution.Projects)
            {
                VCProject vcproj = proj.Object as VCProject;
                if (vcproj == null)
                    continue;
                project_maps[proj] = new Project(proj);
            }
            solevents = dte.Events.SolutionEvents;
            dte.Events.SolutionEvents.ProjectAdded += (EnvDTE.Project proj) =>
            {
                project_maps[proj] = new Project(proj);
            };
            dte.Events.SolutionEvents.ProjectRemoved += (EnvDTE.Project proj) =>
            {
                project_maps.Remove(proj);
            };
            dte.Events.SolutionEvents.BeforeClosing += () =>
            {
                hidden = null;
            };
        }
        
        [Export(typeof(IWpfTextViewConnectionListener))]
        [ContentType("Wide")]
        [TextViewRole(PredefinedTextViewRoles.Analyzable)]
        public class TextViewCreationListener : IWpfTextViewConnectionListener
        {
            public void SubjectBuffersConnected(IWpfTextView textView, ConnectionReason reason, System.Collections.ObjectModel.Collection<ITextBuffer> subjectBuffers)
            {
                foreach (var buffer in subjectBuffers)
                {
                    var filename = ProjectUtils.instance.GetFileName(buffer);
                    if (!ProjectUtils.instance.OpenBuffers.ContainsKey(filename))
                        ProjectUtils.instance.OpenBuffers[filename] = buffer;
                }
            }
            public void SubjectBuffersDisconnected(IWpfTextView textView, ConnectionReason reason, System.Collections.ObjectModel.Collection<ITextBuffer> subjectBuffers)
            {
                foreach (var buffer in subjectBuffers)
                {
                    foreach (var pair in instance.OpenBuffers)
                    {
                        if (pair.Value == buffer)
                        {
                            instance.OpenBuffers.Remove(pair.Key);
                            break;
                        }
                    }
                }
            }
        }
    }
}
