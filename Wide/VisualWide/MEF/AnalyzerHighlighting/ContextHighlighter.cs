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


namespace VisualWide.AnalyzerHighlighting
{
    internal static partial class OrdinaryClassificationDefinition
    {
        [Export(typeof(ClassificationTypeDefinition))]
        [Name("WideModule")]
        internal static ClassificationTypeDefinition WideModuleClassification = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name("WideOverloadSet")]
        internal static ClassificationTypeDefinition WideOverloadSetClassification = null;

        [Export(typeof(ClassificationTypeDefinition))]
        [Name("WideType")]
        internal static ClassificationTypeDefinition WideTypeClassification = null;
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = "WideModule")]
    [Name("WideModule")]
    [UserVisible(false)]
    [Order(Before = Priority.Low)]
    internal class WideModule : ClassificationFormatDefinition
    {
        public WideModule()
        {
            this.DisplayName = "Wide Module"; //human readable version of the name
            this.ForegroundColor = Colors.MediumPurple;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = "WideOverloadSet")]
    [Name("WideOverloadSet")]
    [UserVisible(false)]
    [Order(Before = Priority.Low)]
    internal class WideOverloadSet : ClassificationFormatDefinition
    {
        public WideOverloadSet()
        {
            this.DisplayName = "Wide Overload Set"; //human readable version of the name
            this.ForegroundColor = Colors.MediumVioletRed;
        }
    }

    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = "WideType")]
    [Name("WideType")]
    [UserVisible(false)]
    [Order(Before = Priority.Low)]
    internal class WideType : ClassificationFormatDefinition
    {
        public WideType()
        {
            this.DisplayName = "Wide Type"; //human readable version of the name
            this.ForegroundColor = Colors.MediumSeaGreen;
        }
    }

    [Export(typeof(ITaggerProvider))]
    [ContentType("Wide")]
    [TagType(typeof(ClassificationTag))]
    internal class ContextTaggerProvider : ITaggerProvider
    {
        [Import]
        internal IClassificationTypeRegistryService ClassificationTypeRegistry = null;

        public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
        {
            return new ContextHighlighter(buffer, ClassificationTypeRegistry) as ITagger<T>;
        }
    }

    internal class ContextHighlighter : ITagger<ClassificationTag>
    {
        IClassificationType Module;
        IClassificationType OverloadSet;
        IClassificationType Type;
        ITextBuffer buffer;

        public ContextHighlighter(ITextBuffer lp, IClassificationTypeRegistryService typeService)
        {
            buffer = lp;
            foreach (var proj in ProjectUtils.instance.GetProjectsFor(lp))
            {
                proj.OnUpdate += () =>
                {
                    TagsChanged(this, new SnapshotSpanEventArgs(new SnapshotSpan(lp.CurrentSnapshot, new Span(0, lp.CurrentSnapshot.Length))));
                };
            }

            Module = typeService.GetClassificationType("WideModule");
            OverloadSet = typeService.GetClassificationType("WideOverloadSet");
            Type = typeService.GetClassificationType("WideType");
        }

        public IEnumerable<ITagSpan<ClassificationTag>> GetTags(NormalizedSnapshotSpanCollection spans)
        {
            foreach (var snapspan in ProjectUtils.instance.GetProjectsFor(buffer)
                .Select(proj => proj.AnalyzerQuickInfo.Where(snapspan => snapspan.where.Snapshot == buffer.CurrentSnapshot))
                .Aggregate((x, y) => x.Concat(y)))
            {

                switch (snapspan.type)
                {
                    case ProjectUtils.ContextType.Module:
                        yield return new TagSpan<ClassificationTag>(snapspan.where, new ClassificationTag(Module));
                        break;
                    case ProjectUtils.ContextType.Type:
                        yield return new TagSpan<ClassificationTag>(snapspan.where, new ClassificationTag(Type));
                        break;
                    case ProjectUtils.ContextType.OverloadSet:
                        yield return new TagSpan<ClassificationTag>(snapspan.where, new ClassificationTag(OverloadSet));
                        break;
                }
            }
        }

        public event EventHandler<SnapshotSpanEventArgs> TagsChanged = delegate { };
    }

}
