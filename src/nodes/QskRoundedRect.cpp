/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskRoundedRect.h"
#include "QskGradient.h"
#include "QskGradientDirection.h"
#include "QskBoxShapeMetrics.h"
#include "QskBoxBorderColors.h"
#include "QskBoxBorderMetrics.h"

#include <qmath.h>

namespace
{
    inline int extraStops( const QskGradient& gradient )
    {
        return qMax( 0, gradient.stepCount() - 1 );
    }

    static inline void setGradientLineAt(
        Qt::Orientation orientation, qreal x1, qreal y1, qreal x2, qreal y2,
        const QskGradientStop& stop, QskVertex::ColoredLine* line )
    {
        if ( orientation == Qt::Horizontal )
        {
            const auto pos = x1 + stop.position() * ( x2 - x1 );
            line->setLine( pos, y1, pos, y2, stop.rgb() );
        }
        else
        {
            const auto pos = y1 + stop.position() * ( y2 - y1 );
            line->setLine( x1, pos, x2, pos, stop.rgb() );
        }
    }

   class BorderValue
    {
      public:
        BorderValue( const QskRoundedRect::Metrics::Corner& c )
        {
            if ( c.radiusInnerX >= 0.0 )
            {
                m_inner.x0 = 0.0;
                m_inner.rx = c.radiusInnerX;
            }
            else
            {
                // should also be c.isCropped !
                m_inner.x0 = c.radiusInnerX;
                m_inner.rx = 0.0;
            }

            if ( c.radiusInnerY >= 0.0 )
            {
                m_inner.y0 = 0.0;
                m_inner.ry = c.radiusInnerY;
            }
            else
            {
                // should also be c.isCropped !
                m_inner.y0 = c.radiusInnerY;
                m_inner.ry = 0.0;
            }

            m_outer.x0 = m_outer.y0 = 0.0;
            m_outer.rx = c.radiusX;
            m_outer.ry = c.radiusY;
        }

        void setAngle( qreal cos, qreal sin )
        {
            m_inner.setAngle( cos, sin );
            m_outer.setAngle( cos, sin );
        }

        inline qreal dx1() const { return m_inner.dx; }
        inline qreal dy1() const { return m_inner.dy; }
        inline qreal dx2() const { return m_outer.dx; }
        inline qreal dy2() const { return m_outer.dy; }

      private:
        class Values
        {
          public:
            inline void setAngle( qreal cos, qreal sin )
            {
                dx = x0 + cos * rx;
                dy = y0 + sin * ry;
            }

            qreal dx, dy;
            qreal x0, y0, rx, ry;
        };

        Values m_inner;
        Values m_outer;
    };

    class ColorMap
    {
      public:
        inline ColorMap( const QskGradient& gradient )
            : m_isMonochrome( gradient.isMonochrome() )
            , m_color1( gradient.rgbStart() )
            , m_color2( gradient.rgbEnd() )
        {
            if ( !m_isMonochrome )
            {
                const auto dir = gradient.linearDirection();

                m_x = dir.x1();
                m_y = dir.y1();
                m_dx = dir.x2() - dir.x1();
                m_dy = dir.y2() - dir.y1();
                m_dot = m_dx * m_dx + m_dy * m_dy;
            }
        }

        void setLine( qreal x1, qreal y1, qreal x2, qreal y2,
            QskVertex::ColoredLine* line ) const
        {
            if ( m_isMonochrome )
            {
                line->setLine( x1, y1, x2, y2, m_color1 );
            }
            else
            {
                const auto c1 = colorAt( x1, y1 );
                const auto c2 = colorAt( x2, y2 );

                line->setLine( x1, y1, c1, x2, y2, c2 );
            }
        }

      private:
        QskVertex::Color colorAt( qreal x, qreal y ) const
        {
            if ( m_isMonochrome )
                return m_color1;

            return m_color1.interpolatedTo( m_color2, valueAt( x, y ) );
        }

        inline qreal valueAt( qreal x, qreal y ) const
        {
            const qreal dx = x - m_x;
            const qreal dy = y - m_y;

            return ( dx * m_dx + dy * m_dy ) / m_dot;
        }

        const bool m_isMonochrome;

        qreal m_x, m_y, m_dx, m_dy, m_dot;

        const QskVertex::Color m_color1;
        const QskVertex::Color m_color2;
    };

    class BorderMap
    {
      public:
        inline BorderMap( int stepCount, const QskGradient& g1, const QskGradient& g2 )
            : m_stepCount( stepCount )
            , m_color1( g1.rgbStart() )
            , m_color2( g2.rgbEnd() )
        {
        }

        inline QskVertex::Color colorAt( int step ) const
        {
            if ( m_color1 == m_color2 )
                return m_color1;
            else
                return m_color1.interpolatedTo( m_color2, step / m_stepCount );
        }

      private:
        const qreal m_stepCount;
        const QskVertex::Color m_color1, m_color2;
    };

    class BorderMaps
    {
      public:
        inline BorderMaps( int stepCount, const QskBoxBorderColors& colors )
            : maps{
                { stepCount, colors.top(), colors.left() },
                { stepCount, colors.right(), colors.top() },
                { stepCount, colors.left(), colors.bottom() },
                { stepCount, colors.bottom(), colors.right() } }
        {
        }

        const BorderMap maps[4];
    };
}

int QskRoundedRect::extraBorderStops( const QskBoxBorderColors& bc )
{
    return extraStops( bc.left() ) + extraStops( bc.top() )
        + extraStops( bc.right() ) + extraStops( bc.bottom() );
}


QskRoundedRect::Metrics::Metrics( const QRectF& rect,
        const QskBoxShapeMetrics& shape, const QskBoxBorderMetrics& border )
    : outerQuad( rect )
{
#if 1
    // is this one still needed ?
    isRadiusRegular = shape.isRectellipse();
#endif

    {
        const auto tl = shape.topLeft();
        const auto tr = shape.topRight();
        const auto bl = shape.bottomLeft();
        const auto br = shape.bottomRight();

        if ( tl.isEmpty() || tr.isEmpty() || ( tl.height() == tr.height() ) )
        {
            if ( bl.isEmpty() || br.isEmpty() || ( bl.height() == br.height() ) )
                stepSizeSymmetries |= Qt::Vertical;
        }

        if ( tl.isEmpty() || bl.isEmpty() || ( tl.width() == bl.width() ) )
        {
            if ( tr.isEmpty() || br.isEmpty() || ( tr.width() == br.width() ) )
                stepSizeSymmetries |= Qt::Horizontal;
        }
    }

    for ( int i = 0; i < 4; i++ )
    {
        auto& c = corner[ i ];

        const QSizeF radius = shape.radius( static_cast< Qt::Corner >( i ) );
        c.radiusX = qBound( 0.0, radius.width(), 0.5 * outerQuad.width );
        c.radiusY = qBound( 0.0, radius.height(), 0.5 * outerQuad.height );
        c.stepCount = ArcIterator::segmentHint( qMax( c.radiusX, c.radiusY ) );

        switch ( i )
        {
            case TopLeft:
                c.centerX = outerQuad.left + c.radiusX;
                c.centerY = outerQuad.top + c.radiusY;
                break;

            case TopRight:
                c.centerX = outerQuad.right - c.radiusX;
                c.centerY = outerQuad.top + c.radiusY;
                break;

            case BottomLeft:
                c.centerX = outerQuad.left + c.radiusX;
                c.centerY = outerQuad.bottom - c.radiusY;
                break;

            case BottomRight:
                c.centerX = outerQuad.right - c.radiusX;
                c.centerY = outerQuad.bottom - c.radiusY;
                break;
        }
    }

    centerQuad.left = qMax( corner[ TopLeft ].centerX,
        corner[ BottomLeft ].centerX );

    centerQuad.right = qMin( corner[ TopRight ].centerX,
        corner[ BottomRight ].centerX );

    centerQuad.top = qMax( corner[ TopLeft ].centerY,
        corner[ TopRight ].centerY );

    centerQuad.bottom = qMin( corner[ BottomLeft ].centerY,
        corner[ BottomRight ].centerY );

    centerQuad.width = centerQuad.right - centerQuad.left;
    centerQuad.height = centerQuad.bottom - centerQuad.top;

    // now the bounding rectangle of the fill area

    const auto bw = border.widths();

    innerQuad.left = outerQuad.left + bw.left();
    innerQuad.right = outerQuad.right - bw.right();
    innerQuad.top = outerQuad.top + bw.top();
    innerQuad.bottom = outerQuad.bottom - bw.bottom();

    innerQuad.left = qMin( innerQuad.left, centerQuad.right );
    innerQuad.right = qMax( innerQuad.right, centerQuad.left );
    innerQuad.top = qMin( innerQuad.top, centerQuad.bottom );
    innerQuad.bottom = qMax( innerQuad.bottom, centerQuad.top );

    if ( innerQuad.left > innerQuad.right )
    {
        innerQuad.left = innerQuad.right =
            innerQuad.right + 0.5 * ( innerQuad.left - innerQuad.right );
    }

    if ( innerQuad.top > innerQuad.bottom )
    {
        innerQuad.top = innerQuad.bottom =
            innerQuad.bottom + 0.5 * ( innerQuad.top - innerQuad.bottom );
    }

    innerQuad.width = innerQuad.right - innerQuad.left;
    innerQuad.height = innerQuad.bottom - innerQuad.top;

    const qreal borderLeft = innerQuad.left - outerQuad.left;
    const qreal borderTop = innerQuad.top - outerQuad.top;
    const qreal borderRight = outerQuad.right - innerQuad.right;
    const qreal borderBottom = outerQuad.bottom - innerQuad.bottom;

    for ( int i = 0; i < 4; i++ )
    {
        auto& c = corner[ i ];

        switch ( i )
        {
            case TopLeft:
            {
                c.radiusInnerX = c.radiusX - borderLeft;
                c.radiusInnerY = c.radiusY - borderTop;

                c.isCropped = ( c.centerX <= innerQuad.left ) ||
                    ( c.centerY <= innerQuad.top );

                break;
            }
            case TopRight:
            {
                c.radiusInnerX = c.radiusX - borderRight;
                c.radiusInnerY = c.radiusY - borderTop;

                c.isCropped = ( c.centerX >= innerQuad.right ) ||
                    ( c.centerY <= innerQuad.top );
                break;
            }
            case BottomLeft:
            {
                c.radiusInnerX = c.radiusX - borderLeft;
                c.radiusInnerY = c.radiusY - borderBottom;

                c.isCropped = ( c.centerX <= innerQuad.left ) ||
                    ( c.centerY >= innerQuad.bottom );
                break;
            }
            case BottomRight:
            {
                c.radiusInnerX = c.radiusX - borderRight;
                c.radiusInnerY = c.radiusY - borderBottom;

                c.isCropped = ( c.centerX >= innerQuad.right ) ||
                    ( c.centerY >= innerQuad.bottom );
                break;
            }
        }
    }

    isTotallyCropped =
        corner[ TopLeft ].isCropped &&
        corner[ TopRight ].isCropped &&
        corner[ BottomRight ].isCropped &&
        corner[ BottomLeft ].isCropped;

    // number of steps for iterating over the corners

    isBorderRegular =
        ( borderLeft == borderTop ) &&
        ( borderTop == borderRight ) &&
        ( borderRight == borderBottom );
}

QskRoundedRect::BorderValues::BorderValues( const QskRoundedRect::Metrics& metrics )
    : m_metrics( metrics )
    , m_isUniform( metrics.isBorderRegular && metrics.isRadiusRegular )
{
    if ( m_isUniform )
    {
        const auto& c = metrics.corner[ 0 ];

        m_uniform.dx1 = c.radiusInnerX;
        m_uniform.dy1 = c.radiusInnerY;
    }
    else
    {
        for ( int i = 0; i < 4; i++ )
        {
            const auto& c = metrics.corner[ i ];

            auto& inner = m_multi.inner[ i ];
            auto& outer = m_multi.outer[ i ];

            if ( c.radiusInnerX >= 0.0 )
            {
                inner.x0 = 0.0;
                inner.rx = c.radiusInnerX;
            }
            else
            {
                // should also be c.isCropped !
                inner.x0 = c.radiusInnerX;
                inner.rx = 0.0;
            }

            if ( c.radiusInnerY >= 0.0 )
            {
                inner.y0 = 0.0;
                inner.ry = c.radiusInnerY;
            }
            else
            {
                // should also be c.isCropped !
                inner.y0 = c.radiusInnerY;
                inner.ry = 0.0;
            }

            outer.x0 = outer.y0 = 0.0;
            outer.rx = c.radiusX;
            outer.ry = c.radiusY;
        }
    }
}

void QskRoundedRect::BorderValues::setAngle( qreal cos, qreal sin )
{
    if ( m_isUniform )
    {
        const auto& c = m_metrics.corner[ 0 ];

        if ( !c.isCropped )
        {
            m_uniform.dx1 = cos * c.radiusInnerX;
            m_uniform.dy1 = sin * c.radiusInnerY;
        }

        m_uniform.dx2 = cos * c.radiusX;
        m_uniform.dy2 = sin * c.radiusY;
    }
    else
    {
        auto inner = m_multi.inner;
        auto outer = m_multi.outer;

        inner[ 0 ].setAngle( cos, sin );
        inner[ 1 ].setAngle( cos, sin );
        inner[ 2 ].setAngle( cos, sin );
        inner[ 3 ].setAngle( cos, sin );

        outer[ 0 ].setAngle( cos, sin );
        if ( !m_metrics.isRadiusRegular )
        {
            outer[ 1 ].setAngle( cos, sin );
            outer[ 2 ].setAngle( cos, sin );
            outer[ 3 ].setAngle( cos, sin );
        }
    }
}

void QskRoundedRect::Stroker::setBorderGradientLines(
    Qt::Edge edge, const QskBoxBorderColors& colors,
    QskVertex::ColoredLine* lines ) const
{
    const auto& gradient = colors.gradientAt( edge );
    if( gradient.stepCount() <= 1 )
    {
        // everything done as contour lines
        return;
    }

    qreal x1, x2, y1, y2;
    Qt::Orientation orientation;

    switch( edge )
    {
        case Qt::LeftEdge:
        {
            const auto& c1 = m_metrics.corner[ BottomLeft ];
            const auto& c2 = m_metrics.corner[ TopLeft ];

            orientation = Qt::Vertical;

            x1 = m_metrics.innerQuad.left;
            x2 = m_metrics.outerQuad.left;
            y1 = c1.isCropped ? c1.centerY + c1.radiusInnerY : c1.centerY;
            y2 = c2.isCropped ? c2.centerY - c2.radiusInnerY : c2.centerY;

            break;
        }
        case Qt::TopEdge:
        {
            const auto& c1 = m_metrics.corner[ TopLeft ];
            const auto& c2 = m_metrics.corner[ TopRight ];

            orientation = Qt::Horizontal;

            x1 = c1.isCropped ? c1.centerX - c1.radiusInnerX : c1.centerX;
            x2 = c2.isCropped ? c2.centerX + c2.radiusInnerX : c2.centerX;
            y1 = m_metrics.innerQuad.top;
            y2 = m_metrics.outerQuad.top;

            break;
        }
        case Qt::BottomEdge:
        {
            const auto& c1 = m_metrics.corner[ BottomRight ];
            const auto& c2 = m_metrics.corner[ BottomLeft ];

            orientation = Qt::Horizontal;

            x1 = c1.isCropped ? c1.centerX + c1.radiusInnerX : c1.centerX;
            x2 = c2.isCropped ? c2.centerX - c2.radiusInnerX : c2.centerX;
            y1 = m_metrics.innerQuad.bottom;
            y2 = m_metrics.outerQuad.bottom;

            break;
        }
        case Qt::RightEdge:
        {
            const auto& c1 = m_metrics.corner[ TopRight ];
            const auto& c2 = m_metrics.corner[ BottomRight ];

            orientation = Qt::Vertical;

            x1 = m_metrics.innerQuad.right;
            x2 = m_metrics.outerQuad.right;
            y1 = c1.isCropped ? c1.centerY + c1.radiusInnerY : c1.centerY;
            y2 = c2.isCropped ? c2.centerY - c2.radiusInnerY : c2.centerY;

            break;
        }
    }

    auto line = lines;
    const auto& stops = gradient.stops();

    if ( stops.last().position() < 1.0 )
        setGradientLineAt( orientation, x1, y1, x2, y2, stops.last(), line++ );

    for( int i = stops.count() - 2; i >= 1; i-- )
        setGradientLineAt( orientation, x1, y1, x2, y2, stops[i], line++ );

    if ( stops.first().position() > 0.0 )
        setGradientLineAt( orientation, x1, y1, x2, y2, stops.first(), line++ );
}

void QskRoundedRect::Stroker::createBorderLines( QskVertex::Line* lines ) const
{
    Q_ASSERT( m_metrics.isRadiusRegular );

    const auto& c = m_metrics.corner;

    const int stepCount = c[ 0 ].stepCount;
    const int numCornerLines = stepCount + 1;

    QskVertex::Line* linesBR = lines;
    QskVertex::Line* linesTR = linesBR + numCornerLines;
    QskVertex::Line* linesTL = linesTR + numCornerLines;
    QskVertex::Line* linesBL = linesTL + numCornerLines;

    BorderValues v( m_metrics );

    /*
        It would be possible to run over [0, 0.5 * M_PI_2]
        and create 8 values ( instead of 4 ) in each step. TODO ...
     */
    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        v.setAngle( it.cos(), it.sin() );

        const int j = it.step();
        const int k = numCornerLines - it.step() - 1;

        {
            constexpr auto corner = TopLeft;

            linesTL[ j ].setLine(
                c[ corner ].centerX - v.dx1( corner ),
                c[ corner ].centerY - v.dy1( corner ),
                c[ corner ].centerX - v.dx2( corner ),
                c[ corner ].centerY - v.dy2( corner ) );
        }

        {
            constexpr auto corner = TopRight;

            linesTR[ k ].setLine(
                c[ corner ].centerX + v.dx1( corner ),
                c[ corner ].centerY - v.dy1( corner ),
                c[ corner ].centerX + v.dx2( corner ),
                c[ corner ].centerY - v.dy2( corner ) );
        }

        {
            constexpr auto corner = BottomLeft;

            linesBL[ k ].setLine(
                c[ corner ].centerX - v.dx1( corner ),
                c[ corner ].centerY + v.dy1( corner ),
                c[ corner ].centerX - v.dx2( corner ),
                c[ corner ].centerY + v.dy2( corner ) );
        }

        {
            constexpr auto corner = BottomRight;

            linesBR[ j ].setLine(
                c[ corner ].centerX + v.dx1( corner ),
                c[ corner ].centerY + v.dy1( corner ),
                c[ corner ].centerX + v.dx2( corner ),
                c[ corner ].centerY + v.dy2( corner ) );
        }
    }

    lines[ 4 * numCornerLines ] = lines[ 0 ];
}

void QskRoundedRect::Stroker::createBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    if ( m_metrics.isRadiusRegular )
        createRegularBox( borderLines, borderColors, fillLines, gradient );
    else
        createIrregularBox( borderLines, borderColors, fillLines, gradient );
}

void QskRoundedRect::Stroker::createRegularBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    Q_ASSERT( fillLines == nullptr || ( gradient.isValid() && gradient.stepCount() <= 1 ) );

    const ColorMap fillMap( gradient );

    const auto& c = m_metrics.corner;
    const int stepCount = c[ 0 ].stepCount;

    const BorderMaps borderMaps( stepCount, borderColors );

    QskVertex::ColoredLine* linesBR, * linesTR, * linesTL, * linesBL;
    linesBR = linesTR = linesTL = linesBL = nullptr;

    const int numCornerLines = stepCount + 1;
    const int numFillLines = fillLines ? 2 * numCornerLines : 0;

    if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
    {
        if ( borderLines )
        {
            linesTR = borderLines + 1;
            linesTL = linesTR + numCornerLines + extraStops( borderColors.top() );
            linesBL = linesTL + numCornerLines + extraStops( borderColors.left() );
            linesBR = linesBL + numCornerLines + extraStops( borderColors.bottom() );
        }
    }
    else
    {
        if ( borderLines )
        {
            linesBR = borderLines;
            linesTR = linesBR + numCornerLines + extraStops( borderColors.right() );
            linesTL = linesTR + numCornerLines + extraStops( borderColors.top() );
            linesBL = linesTL + numCornerLines + extraStops( borderColors.left() );
        }
    }

    BorderValues v( m_metrics );

    /*
        It would be possible to run over [0, 0.5 * M_PI_2]
        and create 8 values ( instead of 4 ) in each step. TODO ...
     */
    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        v.setAngle( it.cos(), it.sin() );

        if ( borderLines )
        {
            const int j = it.step();
            const int k = numCornerLines - it.step() - 1;

            {
                constexpr auto corner = TopLeft;

                linesTL[ j ].setLine(
                    c[ corner ].centerX - v.dx1( corner ),
                    c[ corner ].centerY - v.dy1( corner ),
                    c[ corner ].centerX - v.dx2( corner ),
                    c[ corner ].centerY - v.dy2( corner ),
                    borderMaps.maps[ corner ].colorAt( j ) );
            }

            {
                constexpr auto corner = TopRight;

                linesTR[ k ].setLine(
                    c[ corner ].centerX + v.dx1( corner ),
                    c[ corner ].centerY - v.dy1( corner ),
                    c[ corner ].centerX + v.dx2( corner ),
                    c[ corner ].centerY - v.dy2( corner ),
                    borderMaps.maps[ corner ].colorAt( k ) );
            }

            {
                constexpr auto corner = BottomLeft;

                linesBL[ k ].setLine(
                    c[ corner ].centerX - v.dx1( corner ),
                    c[ corner ].centerY + v.dy1( corner ),
                    c[ corner ].centerX - v.dx2( corner ),
                    c[ corner ].centerY + v.dy2( corner ),
                    borderMaps.maps[ corner ].colorAt( k ) );
            }

            {
                constexpr auto corner = BottomRight;

                linesBR[ j ].setLine(
                    c[ corner ].centerX + v.dx1( corner ),
                    c[ corner ].centerY + v.dy1( corner ),
                    c[ corner ].centerX + v.dx2( corner ),
                    c[ corner ].centerY + v.dy2( corner ),
                    borderMaps.maps[ corner ].colorAt( j ) );
            }
        }

        if ( fillLines )
        {
            if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
            {
                const int j = stepCount - it.step();
                const int k = numFillLines - 1 - stepCount + it.step();

                const qreal x1 = c[ TopLeft ].centerX - v.dx1( TopLeft );
                const qreal y11 = c[ TopLeft ].centerY - v.dy1( TopLeft );
                const qreal y12 = c[ BottomLeft ].centerY + v.dy1( BottomLeft );

                const qreal x2 = c[ TopRight ].centerX + v.dx1( TopRight );
                const qreal y21 = c[ TopRight ].centerY - v.dy1( TopRight );
                const qreal y22 = c[ BottomRight ].centerY + v.dy1( BottomRight );

                fillMap.setLine( x1, y11, x1, y12, fillLines + j );
                fillMap.setLine( x2, y21, x2, y22, fillLines + k );
            }
            else
            {
                const int j = it.step();
                const int k = numFillLines - it.step() - 1;

                const qreal x11 = c[ TopLeft ].centerX - v.dx1( TopLeft );
                const qreal x12 = c[ TopRight ].centerX + v.dx1( TopRight );
                const qreal y1 = c[ TopLeft ].centerY - v.dy1( TopLeft );

                const qreal x21 = c[ BottomLeft ].centerX - v.dx1( BottomLeft );
                const qreal x22 = c[ BottomRight ].centerX + v.dx1( BottomRight );
                const qreal y2 = c[ BottomLeft ].centerY + v.dy1( BottomLeft );

                fillMap.setLine( x11, y1, x12, y1, fillLines + j );
                fillMap.setLine( x21, y2, x22, y2, fillLines + k );
            }
        }
    }

    if ( borderLines )
    {
        setBorderGradientLines( Qt::TopEdge, borderColors, linesTR + numCornerLines );
        setBorderGradientLines( Qt::BottomEdge, borderColors, linesBL + numCornerLines );
        setBorderGradientLines( Qt::LeftEdge, borderColors, linesTL + numCornerLines );
        setBorderGradientLines( Qt::RightEdge, borderColors, linesBR + numCornerLines );

        const int k = 4 * numCornerLines + extraBorderStops( borderColors );

        if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
            borderLines[ 0 ] = borderLines[ k ];
        else
            borderLines[ k ] = borderLines[ 0 ];
    }
}

void QskRoundedRect::Stroker::createIrregularBorder(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors ) const
{
    const auto& c = m_metrics.corner;

    QskVertex::ColoredLine* linesBR, * linesTR, * linesTL, * linesBL;
    QskVertex::ColoredLine* linesRight, * linesTop, * linesLeft, * linesBottom;

    if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
    {
        linesTR = borderLines + 1;
        linesTop = linesTR + c[ TopRight ].stepCount + 1;

        linesTL = linesTop + extraStops( borderColors.top() );
        linesLeft = linesTL + c[ TopLeft ].stepCount + 1;

        linesBL = linesLeft + extraStops( borderColors.left() );
        linesBottom = linesBL + c[ BottomLeft ].stepCount + 1;

        linesBR = linesBottom + extraStops( borderColors.bottom() );
        linesRight = linesBR + c[ BottomRight ].stepCount + 1;
    }
    else
    {
        linesBR = borderLines;
        linesRight = linesBR + c[ BottomRight ].stepCount + 1;

        linesTR = linesRight + extraStops( borderColors.right() );
        linesTop = linesTR + c[ TopRight ].stepCount + 1;

        linesTL = linesTop + extraStops( borderColors.top() );
        linesLeft = linesTL + c[ TopLeft ].stepCount + 1;

        linesBL = linesLeft + extraStops( borderColors.left() );
        linesBottom = linesBL + c[ BottomLeft ].stepCount + 1;
    }

    {
        const auto& c = m_metrics.corner[ TopLeft ];

        const BorderMap map( c.stepCount, borderColors.top(), borderColors.left() );
        BorderValue v( c );

        for ( ArcIterator it( c.stepCount, false ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            linesTL[ it.step() ].setLine(
                c.centerX - v.dx1(), c.centerY - v.dy1(),
                c.centerX - v.dx2(), c.centerY - v.dy2(),
                map.colorAt( it.step() ) );
        }
    }

    {
        const auto& c = m_metrics.corner[ BottomLeft ];

        const BorderMap map( c.stepCount, borderColors.bottom(), borderColors.left() );
        BorderValue v( c );

        for ( ArcIterator it( c.stepCount, true ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            linesBL[ it.step() ].setLine(
                c.centerX - v.dx1(), c.centerY + v.dy1(),
                c.centerX - v.dx2(), c.centerY + v.dy2(),
                map.colorAt( it.step() ) );
        }
    }

    {
        const auto& c = m_metrics.corner[ BottomRight ];

        const BorderMap map( c.stepCount, borderColors.bottom(), borderColors.right() );
        BorderValue v( c );

        for ( ArcIterator it( c.stepCount, false ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            linesBR[ it.step() ].setLine(
                c.centerX + v.dx1(), c.centerY + v.dy1(),
                c.centerX + v.dx2(), c.centerY + v.dy2(),
                map.colorAt( it.step() ) );
        }
    }

    {
        const auto& c = m_metrics.corner[ TopRight ];

        const BorderMap map( c.stepCount, borderColors.top(), borderColors.right() );
        BorderValue v( c );

        for ( ArcIterator it( c.stepCount, true ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            linesTR[ it.step() ].setLine(
                c.centerX + v.dx1(), c.centerY - v.dy1(),
                c.centerX + v.dx2(), c.centerY - v.dy2(),
                map.colorAt( it.step() ) );
        }
    }

    setBorderGradientLines( Qt::TopEdge, borderColors, linesTop );
    setBorderGradientLines( Qt::BottomEdge, borderColors, linesBottom );
    setBorderGradientLines( Qt::LeftEdge, borderColors, linesLeft );
    setBorderGradientLines( Qt::RightEdge, borderColors, linesRight );

    const int k = c[0].stepCount + c[1].stepCount
        + c[2].stepCount + c[3].stepCount + 4 + extraBorderStops( borderColors );

    if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
        borderLines[ 0 ] = borderLines[ k ];
    else
        borderLines[ k ] = borderLines[ 0 ];
}

void QskRoundedRect::Stroker::createIrregularFill(
    QskVertex::ColoredLine* lines, const QskGradient& gradient ) const
{
    const auto& c = m_metrics.corner;

    BorderValues v( m_metrics );

    const ColorMap fillMap( gradient );

    auto line = lines;

    int stepCount;

    if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
    {
        stepCount = qMax( c[TopLeft].stepCount, c[BottomLeft].stepCount );

        for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            const qreal x = c[ TopLeft ].centerX - v.dx1( TopLeft );
            const qreal y1 = c[ TopLeft ].centerY - v.dy1( TopLeft );
            const qreal y2 = c[ BottomLeft ].centerY + v.dy1( BottomLeft );

            fillMap.setLine( x, y1, x, y2, line++ );
        }

        stepCount = qMax( c[TopRight].stepCount, c[BottomRight].stepCount );

        for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            const qreal x = c[ TopRight ].centerX + v.dx1( TopRight );
            const qreal y1 = c[ TopRight ].centerY - v.dy1( TopRight );
            const qreal y2 = c[ BottomRight ].centerY + v.dy1( BottomRight );

            fillMap.setLine( x, y1, x, y2, line++ );
        }
    }
    else
    {
        stepCount = qMax( c[TopLeft].stepCount, c[TopRight].stepCount );

        for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            const qreal x1 = c[ TopLeft ].centerX - v.dx1( TopLeft );
            const qreal x2 = c[ TopRight ].centerX + v.dx1( TopRight );
            const qreal y = c[ TopLeft ].centerY - v.dy1( TopLeft );

            fillMap.setLine( x1, y, x2, y, line++ );
        }

        stepCount = qMax( c[BottomLeft].stepCount, c[BottomRight].stepCount );

        for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
        {
            v.setAngle( it.cos(), it.sin() );

            const qreal x1 = c[ BottomLeft ].centerX - v.dx1( BottomLeft );
            const qreal x2 = c[ BottomRight ].centerX + v.dx1( BottomRight );
            const qreal y = c[ BottomLeft ].centerY + v.dy1( BottomLeft );

            fillMap.setLine( x1, y, x2, y, line++ );
        }
    }
}

void QskRoundedRect::Stroker::createIrregularBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    Q_ASSERT( fillLines == nullptr || ( gradient.isValid() && gradient.stepCount() <= 1 ) );

    if ( borderLines )
        createIrregularBorder( borderLines, borderColors );

    if ( fillLines )
        createIrregularFill( fillLines, gradient );
}

int QskRoundedRect::Stroker::borderLineCount( const QskBoxBorderColors& colors ) const
{
    if ( !colors.isVisible() || m_metrics.innerQuad == m_metrics.outerQuad )
        return 0;

    const auto c = m_metrics.corner;

    /*
        Number of lines is always one more than the number of steps.
        So we have to add 1 for each corner.
     */
    int n = 4;

    n += c[0].stepCount + c[1].stepCount + c[2].stepCount + c[3].stepCount;
    n += extraBorderStops( colors );

    n++; // duplicating the first line at the end to close the border path

    return n;
}

int QskRoundedRect::Stroker::fillLineCount( const QskGradient& gradient ) const
{
    if ( !gradient.isVisible() )
        return 0;

    if ( m_metrics.isTotallyCropped )
        return 2;

    const auto c = m_metrics.corner;

    /*
        Number of lines is always one more than the number of steps.
        So we have to add 1 for the opening and 1 for the closing
        part.
     */
    int n = 2;

    if ( m_metrics.stepSizeSymmetries == Qt::Horizontal )
    {
        n += qMax( c[ TopLeft ].stepCount, c[ BottomLeft ].stepCount );
        n += qMax( c[ TopRight ].stepCount, c[ BottomRight ].stepCount );
    }   
    else
    {
        n += qMax( c[ TopLeft ].stepCount, c[ TopRight ].stepCount );
        n += qMax( c[ BottomLeft ].stepCount, c[ BottomRight ].stepCount );
    }   

    return n;
}
