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
            public LexerProvider.TokenType type;
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
            public List<List<CombinedError>> combiner_errors = new List<List<CombinedError>>();
        }

        public enum ParserError
        {
            ModuleScopeFunctionNoOpenBracket,
            ModuleScopeOperatorNoOpenBracket,
            UnrecognizedTokenModuleScope,
            NonOverloadableOperator,
            NoOperatorFound
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
            public Outline(SnapshotSpan loc, OutliningType t)
            {
                type = t;
                where = loc;
            }
            public OutliningType type;
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
            public Error(SnapshotSpan whe, ParserError wha)
            {
                where = whe;
                what = wha;
            }
            public SnapshotSpan where;
            public ParserError what;
        }
        public enum DeclType {
            Function,
            Using,
            Type,
            Module,
        };
        public struct CCombinedError
        {
            public LexerProvider.CRange where;
            public DeclType what;
        };
        public struct CombinedError
        {
            public CombinedError(LexerProvider.CRange range, ITextSnapshot shot, DeclType wha)
            {
                if (range.end.offset - range.begin.offset > 1)
                    range.end.offset += 1;
                range.end.offset = range.end.offset > shot.Length ? (uint)(shot.Length) : range.end.offset;
                position = LexerProvider.RangeFromCRange(range);
                where = new SnapshotSpan(shot, LexerProvider.SpanFromLexer(position));
                what = wha;
            }
            public LexerProvider.Range position;
            public SnapshotSpan where;
            public DeclType what;
        }

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate MaybeToken TokenCallback(System.IntPtr con);
        
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void OutlineCallback(LexerProvider.CRange where, OutliningType what, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ErrorCallback(int count, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0)]LexerProvider.CRange[] where, ParserError what, System.IntPtr context);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void WarningCallback(LexerProvider.CRange where, ParserWarning what, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr ParseWide(
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]TokenCallback tokencallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]OutlineCallback outlinecallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]ErrorCallback errorcallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]WarningCallback warningcallback,
            [MarshalAs(UnmanagedType.LPStr)]String filename
        );

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DestroyParser(System.IntPtr parser);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr GetParserErrorString(ParserError err);
        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr GetParserWarningString(ParserWarning err);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void CombinedErrorCallback(int count, [MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 0)]CCombinedError[] errs, System.IntPtr context);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void GetParserCombinedErrors(System.IntPtr parser, CombinedErrorCallback callback, System.IntPtr context);

        public static string GetErrorString(ParserError err)
        {
            return Marshal.PtrToStringAnsi(GetParserErrorString(err));
        }
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
                        result.location.begin.location = result.location.end.location = PrevLocation = Marshal.StringToHGlobalAnsi(enumerator.Current.location.begin.location);
                        result.location.begin.offset = enumerator.Current.location.begin.offset;
                        result.location.begin.line = enumerator.Current.location.begin.line;
                        result.location.begin.column = enumerator.Current.location.begin.column;
                        result.location.end.offset = enumerator.Current.location.end.offset;
                        result.location.end.line = enumerator.Current.location.end.line;
                        result.location.end.column = enumerator.Current.location.end.column;
                        result.type = enumerator.Current.type;
                        result.value = PrevString = Marshal.StringToHGlobalAnsi(enumerator.Current.value);
                        return result;
                    }
                    result.exists = 0;
                    return result;
                },
                (where, type, context) =>
                {
                    results.outlining.Add(new Outline(new SnapshotSpan(shot, LexerProvider.SpanFromLexer(where)), type));
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
                (where, what, context) =>
                {
                    results.warnings.Add(new Warning(new SnapshotSpan(shot, LexerProvider.SpanFromLexer(where)), what));
                },
                ProjectUtils.GetFileName(factory, shot.TextBuffer)
                //utils.GetFileName(shot.TextBuffer)
            );
            GetParserCombinedErrors(currparser,
                (count, array, context) =>
                {
                    var list = new List<CCombinedError>(array);
                    var result = list.Select(orig =>
                    {
                        return new CombinedError(orig.where, shot, orig.what);
                    });
                    results.combiner_errors.Add(new List<CombinedError>(result));
                },
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
        public IEnumerable<IEnumerable<CombinedError>> CombinedErrors
        {
            get
            {
                ParseSnapshot();
                return currentresults.combiner_errors;
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
