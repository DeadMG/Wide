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
    internal class WideTokenTag : ITag {
        public bool IsVariableLength = false;
        public bool IsComment = false;
        public bool IsKeyword = false;
        public bool IsString = false;
        public bool IsInteger = false;
        public Nullable<Lexer.Failure> Error = null;
    }    

    internal class HighlightWordTagger : ITagger<ClassificationTag>
    {
        ITextBuffer SourceBuffer { get; set; }
        ITagAggregator<WideTokenTag> Aggregator { get; set; }
        IClassificationType Keyword;
        IClassificationType Comment;
        IClassificationType Literal;
        
        public HighlightWordTagger(ITextBuffer sourceBuffer, ITagAggregator<WideTokenTag> aggregator, IClassificationTypeRegistryService typeService)
        {
            this.SourceBuffer = sourceBuffer;
            this.Aggregator = aggregator;
            Keyword = typeService.GetClassificationType("WideKeyword");
            Comment = typeService.GetClassificationType("WideComment");
            Literal = typeService.GetClassificationType("WideLiteral");
            sourceBuffer.Changed += (sender, args) =>
            {
                TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(args.After, new Span(0, args.After.Length))));
            };
        }
                
        public IEnumerable<ITagSpan<ClassificationTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            foreach (var span in spans)
            {
                foreach (var tag in Aggregator.GetTags(span))
                {
                    foreach (var reallyspan in tag.Span.GetSpans(SourceBuffer.CurrentSnapshot))
                    {
                        if (tag.Tag.IsKeyword)
                            yield return new TagSpan<ClassificationTag>(reallyspan, new ClassificationTag(Keyword));
                        if (tag.Tag.IsInteger || tag.Tag.IsString)
                            yield return new TagSpan<ClassificationTag>(reallyspan, new ClassificationTag(Literal));
                        if (tag.Tag.IsComment)
                            yield return new TagSpan<ClassificationTag>(reallyspan, new ClassificationTag(Comment));
                        if (tag.Tag.Error != null && tag.Tag.Error.Value == Lexer.Failure.UnterminatedComment)
                            yield return new TagSpan<ClassificationTag>(reallyspan, new ClassificationTag(Comment));
                        else if (tag.Tag.Error != null && tag.Tag.Error.Value == Lexer.Failure.UnterminatedStringLiteral)
                            yield return new TagSpan<ClassificationTag>(reallyspan, new ClassificationTag(Literal));
                    }
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }

    [Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ClassificationTag))]
    internal class HighlightWordTaggerProvider : ITaggerProvider
    {
        [Export]
        [Name("Wide")]
        [BaseDefinition("code")]
        internal static ContentTypeDefinition WideContentType = null;

        [Export]
        [ContentType("Wide")]
        [FileExtension(".wide")]
        internal static FileExtensionToContentTypeDefinition WideFileType = null;

        [Import]
        internal IClassificationTypeRegistryService ClassificationTypeRegistry = null;

        [Import]
        internal IBufferTagAggregatorFactoryService aggregatorFactory = null;

        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            ITagAggregator<WideTokenTag> ookTagAggregator =
                                            aggregatorFactory.CreateTagAggregator<WideTokenTag>(buffer);

            return new HighlightWordTagger(buffer, ookTagAggregator, ClassificationTypeRegistry) as ITagger<T>;
        }
    }

    [Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(WideTokenTag))]
    internal sealed class WideTokenTagProvider : ITaggerProvider
    {
        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new WideTokenTagger(buffer) as ITagger<T>;
        }
    }
    
    internal sealed class WideTokenTagger : ITagger<WideTokenTag>
    {
        Lexer lexer;
        ITextBuffer TextBuffer;
        Dictionary<ITextSnapshot, List<TagSpan<WideTokenTag>>> SnapshotResults = new Dictionary<ITextSnapshot, List<TagSpan<WideTokenTag>>>();

        Span SpanFromLexer(Lexer.Range range) {
            return new Span((int)range.begin.offset, (int)(range.end.offset - range.begin.offset));
        }
        
        void LexSnapshot(ITextSnapshot shot)
        {
            if (SnapshotResults.ContainsKey(shot))
                return;

            var list = new List<TagSpan<WideTokenTag>>();
            SnapshotResults[shot] = list;
            lexer.SetContents(shot.GetText());
            Nullable<Lexer.Token> tok;
            while (true)
            {
                bool error = false;
                
                tok = lexer.Read(except =>
                {
                    WideTokenTag tag = null;
                    Span loc;
                    error = true;
                    if (except.what == Lexer.Failure.UnlexableCharacter)
                    {
                        loc = new Span(
                            (int)except.where.offset,
                            1
                        );
                    }
                    else
                    {
                        loc = new Span(
                            (int)except.where.offset,
                            (int)shot.Length - (int)except.where.offset
                        );
                    }
                    tag = new WideTokenTag();
                    tag.Error = except.what;
                    list.Add(new TagSpan<WideTokenTag>(new SnapshotSpan(shot, loc), tag));
                }, where =>
                {
                    Span loc;
                    WideTokenTag tag = null;
                    tag = new WideTokenTag();
                    tag.IsComment = true;
                    tag.IsVariableLength = true;
                    // Clamp this so it doesn't go over the end when we add \n in the lexer.
                    where.end.offset = where.end.offset > shot.Length ? (uint)(shot.Length) : where.end.offset;
                    loc = SpanFromLexer(where);
                    list.Add(new TagSpan<WideTokenTag>(new SnapshotSpan(shot, loc), tag));
                });

                if (tok == null)
                    if (!error)
                        return;
                    else
                        continue;
                
                var token = tok.Value;
                var location = SpanFromLexer(token.location);
                if (token.type == Lexer.TokenType.String)
                {
                    var tag = new WideTokenTag();
                    tag.IsVariableLength = true;
                    tag.IsString = true;
                    list.Add(new TagSpan<WideTokenTag>(new SnapshotSpan(shot, location), tag));
                }
                if (token.type == Lexer.TokenType.Integer)
                {
                    var tag = new WideTokenTag();
                    tag.IsVariableLength = true;
                    tag.IsInteger = true;
                    list.Add(new TagSpan<WideTokenTag>(new SnapshotSpan(shot, location), tag));
                }
                if (lexer.IsKeyword(token.type))
                {
                    var tag = new WideTokenTag();
                    tag.IsKeyword = true;
                    list.Add(new TagSpan<WideTokenTag>(new SnapshotSpan(shot, location), tag));
                }
            }
        }

        internal WideTokenTagger(ITextBuffer buffer)
        {
            TextBuffer = buffer;
            lexer = new Lexer();

            TextBuffer.Changed += (sender, args) =>
            {
                LexSnapshot(args.After);
                
                TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(args.After, new Span(0, args.After.Length))));
            };
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };

        public IEnumerable<ITagSpan<WideTokenTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            LexSnapshot(spans[0].Snapshot);
            foreach (var snapshotspan in SnapshotResults[spans[0].Snapshot])
            {
                foreach (var span in spans)
                {
                    if (snapshotspan.Span.IntersectsWith(span))
                        yield return snapshotspan;
                }
            }
        }
    }
}
