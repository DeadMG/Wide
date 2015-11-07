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

namespace VisualWide
{
    public class ParserProvider
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct MaybeToken        
        {
            public LexerProvider.CRange location;
            public System.IntPtr type;
            public System.IntPtr value;
            public byte exists;
        }

        public static ParserProvider GetProviderForBuffer(ITextBuffer buf)
        {
            return buf.Properties.GetOrCreateSingletonProperty(typeof(ParserProvider), () => new ParserProvider(LexerProvider.GetProviderForBuffer(buf), buf));
        }

        private class ParserResults {
            public List<Outline> outlining = new List<Outline>();
            public List<Error> errors = new List<Error>();
            public List<Warning> warnings = new List<Warning>();
        }
        public enum ParserWarning
        {
            SemicolonAfterTypeDefinition
        }
        public enum OutliningType
        {
            Function,
            Module,
            Type
        }

        public struct Outline
        {
            public Outline(SnapshotSpan loc)
            {
                where = loc;
            }
            public SnapshotSpan where;
        }

        public class Warning
        {
            public Warning(SnapshotSpan whe, ParserWarning wha)
            {
                where = whe;
                what = wha;
            }
            public SnapshotSpan where;
            public ParserWarning what;
        }
        public class Error
        {
            public Error(SnapshotSpan whe, string wha)
            {
                where = whe;
                what = wha;
            }
            public SnapshotSpan where;
            public string what;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate MaybeToken TokenCallback(System.IntPtr con);
        
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void OutlineCallback(LexerProvider.CRange where, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ErrorCallback(int count, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0)]LexerProvider.CRange[] where, [MarshalAs(UnmanagedType.LPStr)]string what, System.IntPtr context);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void WarningCallback(LexerProvider.CRange where, ParserWarning what, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr ParseWide(
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]TokenCallback tokencallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]ErrorCallback errorcallback,
            [MarshalAs(UnmanagedType.LPStr)]String filename
        );

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DestroyParser(System.IntPtr parser);
        
        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr GetParserWarningString(ParserWarning err);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void GetOutlining(System.IntPtr builder, OutlineCallback callback, System.IntPtr context);
        
        public static string GetWarningString(ParserWarning war)
        {
            return Marshal.PtrToStringAnsi(GetParserWarningString(war));
        }

        [Import]
        public ITextDocumentFactoryService factory = null;

        LexerProvider lexer;
        ITextBuffer textbuffer;
        System.IntPtr parser = System.IntPtr.Zero;
        ParserResults currentresults = new ParserResults();
        ITextSnapshot currentshot;        
        
        private void ParseSnapshot()
        {
            if (currentshot == textbuffer.CurrentSnapshot)
                return;
            currentshot = textbuffer.CurrentSnapshot;
            var shot = textbuffer.CurrentSnapshot;
            var enumerator = lexer.GetTokens(shot).GetEnumerator();
            var results = new ParserResults();
            System.IntPtr PrevString = System.IntPtr.Zero;
            System.IntPtr PrevLocation = System.IntPtr.Zero;
            var currparser = ParseWide(
                System.IntPtr.Zero,
                (con) =>
                {
                    if (PrevString != System.IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(PrevString);
                        PrevString = System.IntPtr.Zero;
                    }
                    if (PrevLocation != System.IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(PrevLocation);
                        PrevLocation = System.IntPtr.Zero;
                    }
                    MaybeToken result = new MaybeToken();
                    if (enumerator.MoveNext())
                    {
                        result.exists = 1;
                        result.location.begin.location = result.location.end.location = PrevLocation = Marshal.StringToHGlobalAnsi(enumerator.Current.SourceLocation.begin.location);
                        result.location.begin.offset = enumerator.Current.SourceLocation.begin.offset;
                        result.location.begin.line = enumerator.Current.SourceLocation.begin.line;
                        result.location.begin.column = enumerator.Current.SourceLocation.begin.column;
                        result.location.end.offset = enumerator.Current.SourceLocation.end.offset;
                        result.location.end.line = enumerator.Current.SourceLocation.end.line;
                        result.location.end.column = enumerator.Current.SourceLocation.end.column;
                        result.type = enumerator.Current.Type;
                        result.value = PrevString = Marshal.StringToHGlobalAnsi(enumerator.Current.Value);
                        return result;
                    }
                    result.exists = 0;
                    return result;
                },
                (num, where, what, context) =>
                {
                    foreach (var loc in where)
                    {
                        var copy = loc;
                        if (copy.end.offset - copy.begin.offset > 1)
                            copy.end.offset += 1;
                        copy.end.offset = copy.end.offset > shot.Length ? (uint)(shot.Length) : copy.end.offset;
                        
                        results.errors.Add(new Error(new SnapshotSpan(shot, LexerProvider.SpanFromLexer(copy)), what));
                    }
                },
                ProjectUtils.GetFileName(factory, shot.TextBuffer)
                //utils.GetFileName(shot.TextBuffer)
            );
            GetOutlining(
                currparser, 
                (where, con) => results.outlining.Add(new Outline(new SnapshotSpan(shot, LexerProvider.SpanFromLexer(where)))), 
                System.IntPtr.Zero
            );
             
            var oldparser = parser;
            parser = currparser;
            if (oldparser != System.IntPtr.Zero)
                DestroyParser(oldparser);
            currentresults = results;
            
            TagsChanged(new SnapshotSpan(shot, new Span(0, shot.Length)));
        }

        ParserProvider(LexerProvider lp, ITextBuffer buf)
        {
            ProjectUtils.SatisfyMEFImports(this);
            textbuffer = buf;
            lexer = lp;
            ParseSnapshot();
            lexer.TagsChanged += (span) =>
            {
                ParseSnapshot();
            };
        }

        public IEnumerable<Outline> Outlines
        {
            get
            {
                ParseSnapshot();
                return currentresults.outlining;
            }
        }

        public IEnumerable<Error> Errors
        {
            get
            {
                ParseSnapshot();
                return currentresults.errors;
            }
        }
        public IEnumerable<Warning> Warnings
        {
            get
            {
                ParseSnapshot();
                return currentresults.warnings;
            }
        }
        
        public delegate void ContentsChanged(SnapshotSpan span);
        public event ContentsChanged TagsChanged = delegate { };

        public ITextBuffer GetTextBuffer()
        {
            return textbuffer;
        }

        public System.IntPtr GetCurrentParser()
        {
            ParseSnapshot();
            return parser;
        }
    }
}
