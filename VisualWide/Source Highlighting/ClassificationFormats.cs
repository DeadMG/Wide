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

namespace VisualWide
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
}
