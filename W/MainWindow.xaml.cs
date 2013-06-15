using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace W
{
    public partial class MainWindow : Window
    {
        Dictionary<int, TextPointer> TextPointerCache = new Dictionary<int, TextPointer>();
        Dictionary<Tuple<int, int>, TextRange> TextRangeCache = new Dictionary<Tuple<int, int>, TextRange>();
        Dictionary<Tuple<TextPointer, TextPointer>, TextRange> TextRangeFromPointerCache = new Dictionary<Tuple<TextPointer, TextPointer>, TextRange>();

        private TextRange TextRangeFromOffsets(int begin, int end)
        {
            if (TextRangeCache.ContainsKey(System.Tuple.Create(begin, end)))
                return TextRangeCache[System.Tuple.Create(begin, end)];
            return TextRangeCache[System.Tuple.Create(begin, end)] = new TextRange(
                GetPoint(begin),
                GetPoint(end)
            );
        }
        private TextRange TextRangeFromLexer(Lexer.Range where)
        {
            return TextRangeFromOffsets((int)where.begin.offset, (int)where.end.offset);
        }
        private TextRange TextRangeFromPointers(TextPointer begin, TextPointer end)
        {
            if (TextRangeFromPointerCache.ContainsKey(System.Tuple.Create(begin, end)))
                return TextRangeFromPointerCache[System.Tuple.Create(begin, end)];
            return TextRangeFromPointerCache[System.Tuple.Create(begin, end)] = new TextRange(
                begin,
                end
            );
        }
        private TextPointer GetPoint(int x)
        {
            var start = SampleTextBox.Document.ContentStart;
            if (TextPointerCache.ContainsKey(x))
                return TextPointerCache[x];
            var ret = start;
            var i = 0;
            while (ret != null)
            {
                string stringSoFar = TextRangeFromPointers(ret, ret.GetPositionAtOffset(i, LogicalDirection.Forward)).Text;
                if (stringSoFar.Length == x)
                        break;
                i++;
                if (ret.GetPositionAtOffset(i, LogicalDirection.Forward) == null)
                    return ret.GetPositionAtOffset(i - 1, LogicalDirection.Forward);
        
            }
            ret=ret.GetPositionAtOffset(i, LogicalDirection.Forward);
            return TextPointerCache[x] = ret;
        }
        
        SolidColorBrush CommentBrush = new SolidColorBrush(Colors.Green);
        SolidColorBrush KeywordBrush = new SolidColorBrush(Colors.Blue);
        SolidColorBrush LiteralBrush = new SolidColorBrush(Colors.Red); 
        TextDecorationCollection ErrorCollection = new TextDecorationCollection();

        Lexer lexer;

        void OnTextChanged(object sender, TextChangedEventArgs e)
        {
            SampleTextBox.TextChanged -= OnTextChanged;
            TextRange textRange = new TextRange(
                SampleTextBox.Document.ContentStart, 
                SampleTextBox.Document.ContentEnd
            );

            TextRangeCache.Clear();
            TextPointerCache.Clear();
            TextRangeFromPointerCache.Clear();
            textRange.ClearAllProperties();
            lexer.SetContents(textRange.Text);
            try
            {
                Nullable<Lexer.Token> token_opt;
                while (true)
                {
                    try
                    {
                        token_opt = lexer.Read();
                        if (token_opt == null) break;
                        var token = token_opt.Value;

                        if (token.type == Lexer.TokenType.String || token.type == Lexer.TokenType.Integer)
                        {
                            var range = TextRangeFromLexer(token.location);
                            range.ApplyPropertyValue(TextElement.ForegroundProperty, LiteralBrush);
                        }
                        if (lexer.IsKeyword(token.type))
                        {
                            var range = TextRangeFromLexer(token.location);
                            range.ApplyPropertyValue(TextElement.ForegroundProperty, KeywordBrush);
                        }
                    }
                    catch (Lexer.LexerError except)
                    {
                        if (except.what == Lexer.Failure.UnlexableCharacter)
                        {
                            var trange = this.TextRangeFromOffsets(
                                (int)except.where.offset,
                                (int)except.where.offset + 1
                            );
                            trange.ApplyPropertyValue(TextBlock.TextDecorationsProperty, ErrorCollection);
                        }
                        else
                        {
                            var brush = except.what == Lexer.Failure.UnterminatedComment ? CommentBrush : LiteralBrush;
                            var range = TextRangeFromOffsets(
                                (int)except.where.offset,
                                textRange.Text.Length
                            );
                            range.ApplyPropertyValue(TextElement.ForegroundProperty, brush);
                            var err_range = TextRangeFromOffsets(
                                textRange.Text.Length - 2,
                                textRange.Text.Length
                            );
                            err_range.ApplyPropertyValue(TextBlock.TextDecorationsProperty, ErrorCollection);
                        }
                    }
                }
            }
            finally
            {
                SampleTextBox.TextChanged += OnTextChanged;
                e.Handled = true;
            }
        }

        public MainWindow()
        {
            InitializeComponent();
            Application.Current.MainWindow.WindowState = WindowState.Maximized;
            SampleTextBox.TextChanged += OnTextChanged;
            lexer = new Lexer("", where =>
            {
                var range = TextRangeFromLexer(where);
                range.ApplyPropertyValue(TextElement.ForegroundProperty, CommentBrush);
            });

            System.Windows.TextDecoration myUnderline = new System.Windows.TextDecoration();
            var SquigglyErrorLines = new VisualBrush();
            SquigglyErrorLines.Viewbox = new Rect(0, 0, 3, 2);
            SquigglyErrorLines.ViewboxUnits = BrushMappingMode.Absolute;
            SquigglyErrorLines.Viewport = new Rect(0, 0.8, 6, 4);
            SquigglyErrorLines.ViewportUnits = BrushMappingMode.Absolute;
            SquigglyErrorLines.TileMode = TileMode.Tile;

            var path = new Path();
            path.Stroke = new SolidColorBrush(Colors.Red);
            path.StrokeThickness = 0.2;
            path.StrokeEndLineCap = PenLineCap.Square;
            path.StrokeStartLineCap = PenLineCap.Square;
            path.Data = Geometry.Parse("M 0,1 C 1,0 2,2 3,1");
            SquigglyErrorLines.Visual = path;

            Pen myPen = new Pen();
            myPen.Brush = SquigglyErrorLines;
            myPen.Thickness = 6;

            myUnderline.Pen = myPen;
            myUnderline.PenThicknessUnit = TextDecorationUnit.Pixel;
            myUnderline.PenOffset = 1;

            ErrorCollection.Add(myUnderline);
        }
    }
}
