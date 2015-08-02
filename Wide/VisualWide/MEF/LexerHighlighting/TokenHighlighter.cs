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
    namespace SourceHighlighting
    {
        internal static class OrdinaryClassificationDefinition
        {
            [Export(typeof(ClassificationTypeDefinition))]
            [Name("WideKeyword")]
            internal static ClassificationTypeDefinition WideKeywordClassification = null;

            [Export(typeof(ClassificationTypeDefinition))]
            [Name("WideComment")]
            internal static ClassificationTypeDefinition WideCommentClassification = null;

            [Export(typeof(ClassificationTypeDefinition))]
            [Name("WideLiteral")]
            internal static ClassificationTypeDefinition WideLiteralClassification = null;
        }

        [Export(typeof(EditorFormatDefinition))]
        [ClassificationType(ClassificationTypeNames = "WideKeyword")]
        [Name("WideKeyword")]
        [UserVisible(false)]
        [Order(Before = Priority.Default)]
        internal class WideKeyword : ClassificationFormatDefinition
        {
            public WideKeyword()
            {
                this.DisplayName = "Wide Keyword"; //human readable version of the name
                this.ForegroundColor = Color.FromArgb(0xFF, 57, 135, 0xFF);
            }
        }

        [Export(typeof(EditorFormatDefinition))]
        [ClassificationType(ClassificationTypeNames = "WideComment")]
        [Name("WideComment")]
        [UserVisible(false)]
        [Order(Before = Priority.Default)]
        internal class WideComment : ClassificationFormatDefinition
        {
            public WideComment()
            {
                this.DisplayName = "Wide Comment"; //human readable version of the name
                this.ForegroundColor = Color.FromArgb(0xFF, 0, 0xFF, 0);
            }
        }

        [Export(typeof(EditorFormatDefinition))]
        [ClassificationType(ClassificationTypeNames = "WideLiteral")]
        [Name("WideLiteral")]
        [UserVisible(false)]
        [Order(Before = Priority.Default)]
        internal class WideLiteral : ClassificationFormatDefinition
        {
            public WideLiteral()
            {
                this.DisplayName = "Wide Literal"; //human readable version of the name
                this.ForegroundColor = Colors.Red;
            }
        }

        [Export(typeof(ITaggerProvider))]
        [ContentType("Wide")]
        [TagType(typeof(ClassificationTag))]
        internal class HighlightWordTaggerProvider : ITaggerProvider
        {
            [Import]
            internal IClassificationTypeRegistryService ClassificationTypeRegistry = null;

            public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
            {
                return new TokenHighlighter(LexerProvider.GetProviderForBuffer(buffer), ClassificationTypeRegistry) as ITagger<T>;
            }
        }

        internal class TokenHighlighter : ITagger<ClassificationTag>
        {
            LexerProvider provider;
            IClassificationType Keyword;
            IClassificationType Comment;
            IClassificationType Literal;
            
            public TokenHighlighter(LexerProvider lp, IClassificationTypeRegistryService typeService)
            {
                provider = lp;

                provider.TagsChanged += (span) =>
                {
                    TagsChanged(this, new SnapshotSpanEventArgs(span));
                };

                Keyword = typeService.GetClassificationType("WideKeyword");
                Comment = typeService.GetClassificationType("WideComment");
                Literal = typeService.GetClassificationType("WideLiteral");
            }

            public IEnumerable<ITagSpan<ClassificationTag>> GetTags(NormalizedSnapshotSpanCollection spans)
            {
                foreach (var token in provider.GetTokens(spans[0].Snapshot))
                {
                    foreach (var span in spans)
                    {
                        if (token.where.IntersectsWith(span))
                        {
                            if (token.type == LexerProvider.TokenType.String || token.type == LexerProvider.TokenType.Integer)
                            {
                                yield return new TagSpan<ClassificationTag>(token.where, new ClassificationTag(Literal));
                            }
                            if (LexerProvider.IsKeyword(token.type))
                            {
                                yield return new TagSpan<ClassificationTag>(token.where, new ClassificationTag(Keyword));
                            }
                        }
                    }
                }
                foreach (var comment in provider.GetComments(spans[0].Snapshot))
                {
                   yield return new TagSpan<ClassificationTag>(comment, new ClassificationTag(Comment));
                }
                foreach (var error in provider.GetErrors(spans[0].Snapshot))
                {
                    if (error.what == LexerProvider.Failure.UnterminatedComment)
                        yield return new TagSpan<ClassificationTag>(error.where, new ClassificationTag(Comment));
                    if (error.what == LexerProvider.Failure.UnterminatedStringLiteral)
                        yield return new TagSpan<ClassificationTag>(error.where, new ClassificationTag(Literal));
                }
            }
            
            public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
        }
    }
}
