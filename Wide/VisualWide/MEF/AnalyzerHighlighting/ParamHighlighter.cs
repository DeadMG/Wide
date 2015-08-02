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


namespace VisualWide.AnalyzerHighlighting
{
    internal static partial class OrdinaryClassificationDefinition
    {
        [Export(typeof(ClassificationTypeDefinition))]
        [Name("WideParameter")]
        internal static ClassificationTypeDefinition WideParameterClassification = null;
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = "WideParameter")]
    [Name("WideParameter")]
    [UserVisible(false)]
    [Order(Before = Priority.Default)]
    internal class WideParameter : ClassificationFormatDefinition
    {
        public WideParameter()
        {
            this.DisplayName = "Wide Parameter"; //human readable version of the name
            this.ForegroundColor = Colors.Gray;
        }
    }

    //[Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ClassificationTag))]
    internal class ParameterTaggerProvider : ITaggerProvider
    {
        [Import]
        internal IClassificationTypeRegistryService ClassificationTypeRegistry = null;

        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new ParamHighlighter(buffer, ClassificationTypeRegistry) as ITagger<T>;
        }
    }

    internal class ParamHighlighter : ITagger<ClassificationTag>
    {
        IClassificationType Param;
        ITextBuffer buffer;

        public ParamHighlighter(ITextBuffer lp, IClassificationTypeRegistryService typeService)
        {
            buffer = lp;
            foreach (var proj in ProjectUtils.instance.GetProjectsFor(lp))
            {
                proj.OnUpdate += () =>
                {
                    TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(lp.CurrentSnapshot, new Span(0, lp.CurrentSnapshot.Length))));
                };
            }

            Param = typeService.GetClassificationType("WideParameter");
        }

        public IEnumerable<ITagSpan<ClassificationTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            foreach (var snapspan in ProjectUtils.instance.GetProjectsFor(buffer)
                .Select(proj => proj.AnalyzerParameterHighlights.Where(snapspan => snapspan.Snapshot == buffer.CurrentSnapshot))
                .Aggregate((x, y) => x.Concat(y)))
            {
                yield return new TagSpan<ClassificationTag>(snapspan, new ClassificationTag(Param));
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }

}
