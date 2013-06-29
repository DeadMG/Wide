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

namespace VisualWide.ParserHighlighting
{
    internal static class OrdinaryClassificationDefinition
    {
        [Export(typeof(ClassificationTypeDefinition))]
        [Name("WideBraceMatch")]
        internal static ClassificationTypeDefinition WideBraceMatchClassification = null;
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = "WideBraceMatch")]
    [Name("WideBraceMatch")]
    [UserVisible(false)]
    [Order(Before = Priority.Default)]
    internal class WideBraceMatch : ClassificationFormatDefinition
    {
        public WideBraceMatch()
        {
            this.DisplayName = "Wide Keyword"; //human readable version of the name
            this.BackgroundColor = Colors.Blue;
        }
    }

    [Export(typeof(IViewTaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ClassificationTag))]
    class BraceMatchingProvider : IViewTaggerProvider
    {
        [Import]
        internal IClassificationTypeRegistryService ClassificationTypeRegistry = null;

        public ITagger<T> CreateTagger<T>(ITextView View, ITextBuffer buffer) where T : ITag
        {
            return new BraceMatcher(View, LexerProvider.GetProviderForBuffer(buffer), ClassificationTypeRegistry) as ITagger<T>;
        }
    }

    internal class BraceMatcher : ITagger<ClassificationTag>
    {
        LexerProvider provider;
        ITextView View;
        System.Tuple<TagSpan<ClassificationTag>, TagSpan<ClassificationTag>> braces;
        IClassificationType bracematchtype;

        void UpdateForNewPosition(CaretPosition p)
        {
            Dictionary<int, int> Matches = new Dictionary<int, int>();
            Dictionary<int, int> ReverseMatches = new Dictionary<int, int>();
            List<int> CurlyStack = new List<int>();
            List<int> SquareStack = new List<int>();
            List<int> RoundStack = new List<int>();
            foreach (var token in provider.GetTokens(p.BufferPosition.Snapshot))
            {
                var offset = token.where.Start.Position;
                switch (token.type)
                {
                    case LexerProvider.TokenType.OpenBracket:
                        RoundStack.Add(offset);
                        break;
                    case LexerProvider.TokenType.OpenSquareBracket:
                        SquareStack.Add(offset);
                        break;
                    case LexerProvider.TokenType.OpenCurlyBracket:
                        CurlyStack.Add(offset);
                        break;
                    case LexerProvider.TokenType.CloseBracket:
                        // For now, just assume the closest open bracket is the match. Don't error on mismatched, i.e. (] brackets, and don't error if unmatched.
                        if (RoundStack.Count != 0)
                        {
                            Matches[RoundStack.Last()] = offset;
                            ReverseMatches[offset] = RoundStack.Last();
                            RoundStack.RemoveAt(RoundStack.Count - 1);
                        }
                        break;
                    case LexerProvider.TokenType.CloseSquareBracket:
                        // For now, just assume the closest open bracket is the match. Don't error on mismatched, i.e. (] brackets, and don't error if unmatched.
                        if (SquareStack.Count != 0)
                        {
                            Matches[SquareStack.Last()] = offset;
                            ReverseMatches[offset] = SquareStack.Last();
                            SquareStack.RemoveAt(SquareStack.Count - 1);
                        }
                        break;
                    case LexerProvider.TokenType.CloseCurlyBracket:
                        // For now, just assume the closest open bracket is the match. Don't error on mismatched, i.e. (] brackets, and don't error if unmatched.
                        if (CurlyStack.Count != 0)
                        {
                            Matches[CurlyStack.Last()] = offset;
                            ReverseMatches[offset] = CurlyStack.Last();
                            CurlyStack.RemoveAt(CurlyStack.Count - 1);
                        }
                        break;
                }                
            }
            var snappoint = p.BufferPosition;
            if (Matches.ContainsKey(snappoint.Position))
            {
                braces = System.Tuple.Create(
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(snappoint.Position, 1)), new ClassificationTag(bracematchtype)),
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(Matches[snappoint.Position], 1)), new ClassificationTag(bracematchtype))
                );
                TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(snappoint.Snapshot, new Span(0, snappoint.Snapshot.Length))));
                return;
            }
            if (ReverseMatches.ContainsKey(snappoint.Position - 1)) // Highlight from the RIGHT for close brackets
            {
                braces = System.Tuple.Create(
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(snappoint.Position - 1, 1)), new ClassificationTag(bracematchtype)),
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(ReverseMatches[snappoint.Position - 1], 1)), new ClassificationTag(bracematchtype))
                );
                TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(snappoint.Snapshot, new Span(0, snappoint.Snapshot.Length))));
                return;
            }
            if (braces != null)
            {
                braces = null;
                TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(snappoint.Snapshot, new Span(0, snappoint.Snapshot.Length))));
            }
        }
        void ViewLayoutChanged(object sender, TextViewLayoutChangedEventArgs args)
        {
            if (args.NewSnapshot == args.OldSnapshot) return;

            View.LayoutChanged -= ViewLayoutChanged;
            UpdateForNewPosition(View.Caret.Position);
            View.LayoutChanged += ViewLayoutChanged;
        }
        public BraceMatcher(ITextView tview, LexerProvider pp, IClassificationTypeRegistryService ClassificationTypeRegistry)
        {
            bracematchtype = ClassificationTypeRegistry.GetClassificationType("WideBraceMatch");
            provider = pp;
            View = tview;
            View.Caret.PositionChanged += (sender, args) =>
            {
                UpdateForNewPosition(args.NewPosition);
            };
            View.LayoutChanged += ViewLayoutChanged;
        }

        public IEnumerable<ITagSpan<ClassificationTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            if (braces != null)
            {
                yield return braces.Item1;
                yield return braces.Item2;
            }
        }
        
        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }
}
