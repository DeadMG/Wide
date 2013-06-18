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
        [Export(typeof(ITaggerProvider))]
        [ContentType("Wide")]
        [TagType(typeof(ClassificationTag))]
        internal class HighlightWordTaggerProvider : ITaggerProvider
        {
            [Import]
            internal IClassificationTypeRegistryService ClassificationTypeRegistry = null;

            public ITagger<T> CreateTagger<T>(ITextBuffer buffer) where T : ITag
            {
                return new HighlightWordTagger(buffer, ClassificationTypeRegistry) as ITagger<T>;
            }
        }
    }
}
