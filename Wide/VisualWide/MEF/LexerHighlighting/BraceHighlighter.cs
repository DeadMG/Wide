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
        System.Tuple<TagSpan<ClassificationTag>, TagSpan<ClassificationTag>> leftbraces;
        System.Tuple<TagSpan<ClassificationTag>, TagSpan<ClassificationTag>> rightbraces;
        IClassificationType bracematchtype;

        void UpdateForNewPosition(CaretPosition p)
        {
            var Matches = new Dictionary<int, int>();
            var ReverseMatches = new Dictionary<int, int>();
            var Stacks = new Dictionary<int, List<int>>();
            foreach (var token in provider.GetTokens(p.BufferPosition.Snapshot).Where(token => token.BracketType != LexerProvider.BracketType.None))
            {
                var offset = token.SpanLocation.Start.Position;
                switch (token.BracketType)
                {
                    case LexerProvider.BracketType.Open:
                        if (!Stacks.ContainsKey(token.BracketNumber))
                            Stacks[token.BracketNumber] = new List<int>();
                        Stacks[token.BracketNumber].Add(offset);
                        break;
                    case LexerProvider.BracketType.Close:
                        if (!Stacks.ContainsKey(token.BracketNumber))
                            Stacks[token.BracketNumber] = new List<int>();
                        if (Stacks[token.BracketNumber].Count != 0)
                        {
                            Matches[Stacks[token.BracketNumber].Last()] = offset;
                            ReverseMatches[offset] = Stacks[token.BracketNumber].Last();
                            Stacks[token.BracketNumber].RemoveAt(Stacks[token.BracketNumber].Count - 1);
                        }
                        break;
                }                
            }
            var snappoint = p.BufferPosition;
            if (Matches.ContainsKey(snappoint.Position))
            {
                leftbraces = System.Tuple.Create(
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(snappoint.Position, 1)), new ClassificationTag(bracematchtype)),
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(Matches[snappoint.Position], 1)), new ClassificationTag(bracematchtype))
                );
            }
            else
            {
                leftbraces = null;
            }
            if (ReverseMatches.ContainsKey(snappoint.Position - 1)) // Highlight from the RIGHT for close brackets
            {
                rightbraces = System.Tuple.Create(
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(snappoint.Position - 1, 1)), new ClassificationTag(bracematchtype)),
                    new TagSpan<ClassificationTag>(new SnapshotSpan(snappoint.Snapshot, new Span(ReverseMatches[snappoint.Position - 1], 1)), new ClassificationTag(bracematchtype))
                );
                TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(snappoint.Snapshot, new Span(0, snappoint.Snapshot.Length))));
            }
            else
            {
                rightbraces = null;
            }
            TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(snappoint.Snapshot, new Span(0, snappoint.Snapshot.Length))));
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
            if (leftbraces != null)
            {
                yield return leftbraces.Item1;
                yield return leftbraces.Item2;
            }
            if (rightbraces != null)
            {
                yield return rightbraces.Item1;
                yield return rightbraces.Item2;
            }
        }
        
        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }
}
