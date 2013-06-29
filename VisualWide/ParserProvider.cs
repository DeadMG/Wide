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
using System.Runtime.InteropServices;

namespace VisualWide
{
    public class ParserProvider
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct MaybeToken        
        {
            public LexerProvider.Range location;
            public LexerProvider.TokenType type;
            public System.IntPtr value;
            public byte exists;
        }

        public static ParserProvider GetProviderForBuffer(ITextBuffer buf)
        {
            return buf.Properties.GetOrCreateSingletonProperty(typeof(ParserProvider), () => new ParserProvider(LexerProvider.GetProviderForBuffer(buf)));
        }


        private class ParserResults {
            public List<Outline> outlining = new List<Outline>();
            public List<Error> errors = new List<Error>();
            public List<Warning> warnings = new List<Warning>();
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

        Dictionary<ITextSnapshot, ParserResults> SnapshotResults = new Dictionary<ITextSnapshot,ParserResults>();

        
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate MaybeToken TokenCallback(System.IntPtr con);
        
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void OutlineCallback(LexerProvider.Range where, OutliningType what, System.IntPtr con);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void ErrorCallback(LexerProvider.Range where, ParserError what);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate void WarningCallback(LexerProvider.Range where, ParserWarning what);

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void ParseWide(
            System.IntPtr context,
            [MarshalAs(UnmanagedType.FunctionPtr)]TokenCallback tokencallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]OutlineCallback outlinecallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]ErrorCallback errorcallback,
            [MarshalAs(UnmanagedType.FunctionPtr)]WarningCallback warningcallback
        );

        [DllImport("CAPI.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern System.IntPtr GetParserErrorString(ParserError err);

        public static string GetErrorString(ParserError err)
        {
            return Marshal.PtrToStringAnsi(GetParserErrorString(err));
        }
        
        LexerProvider lexer;

        private void ParseSnapshot(ITextSnapshot shot)
        {
            var enumerator = lexer.GetTokens(shot).GetEnumerator();
            SnapshotResults[shot] = new ParserResults();
            var results = SnapshotResults[shot];
            System.IntPtr PrevString = System.IntPtr.Zero;
            ParseWide(
                System.IntPtr.Zero,
                (con) =>
                {
                    if (PrevString != System.IntPtr.Zero)
                    {
                        Marshal.FreeHGlobal(PrevString);
                        PrevString = System.IntPtr.Zero;
                    }
                    MaybeToken result = new MaybeToken();
                    if (enumerator.MoveNext())
                    {
                        result.exists = 1;
                        result.location = enumerator.Current.location;
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
                (where, what) =>
                {
                    results.errors.Add(new Error(new SnapshotSpan(shot, LexerProvider.SpanFromLexer(where)), what));
                },
                (where, what) =>
                {
                    results.warnings.Add(new Warning(new SnapshotSpan(shot, LexerProvider.SpanFromLexer(where)), what));
                }
            );
        }

        ParserProvider(LexerProvider lp)
        {
            lexer = lp;
            lexer.TagsChanged += (span) =>
            {
                TagsChanged(span);
            };
        }

        public IEnumerable<Outline> GetOutline(ITextSnapshot shot)
        {
            if (!SnapshotResults.ContainsKey(shot))
                ParseSnapshot(shot);
            return SnapshotResults[shot].outlining;
        }
        public IEnumerable<Error> GetErrors(ITextSnapshot shot)
        {
            if (!SnapshotResults.ContainsKey(shot))
                ParseSnapshot(shot);
            return SnapshotResults[shot].errors;
        }
        public IEnumerable<Warning> GetWarnings(ITextSnapshot shot)
        {
            if (!SnapshotResults.ContainsKey(shot))
                ParseSnapshot(shot);
            return SnapshotResults[shot].warnings;
        }
        
        public delegate void ContentsChanged(SnapshotSpan span);
        public event ContentsChanged TagsChanged = delegate { };
    }
}
