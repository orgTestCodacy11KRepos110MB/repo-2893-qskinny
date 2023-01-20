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
#include "QskBoxRendererColorMap.h"

#include <qmath.h>

namespace
{
    inline int gradientLineCount( const QskGradient& borderGradient )
    {
        return qMax( 0, borderGradient.stepCount() - 1 );
    }

    inline int edgeToIndex( Qt::Edge edge ) { return qCountTrailingZeroBits( (quint8) edge ); }
    inline Qt::Edge indexToEdge( int index ) { return static_cast< Qt::Edge >( 1 << index ); }

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

    class BorderMap
    {
      public:
        inline BorderMap( int stepCount,
                const QskBoxBorderColors& colors, Qt::Corner corner )
            : m_stepCount( stepCount )
        {
            switch( corner )
            {
                case Qt::TopLeftCorner:
                {
                    m_color1 = colors.top().rgbStart();
                    m_color2 = colors.left().rgbEnd();

                    break;
                }
                case Qt::TopRightCorner:
                {
                    m_color1 = colors.top().rgbStart();
                    m_color2 = colors.right().rgbEnd();

                    break;
                }
                case Qt::BottomLeftCorner:
                {
                    m_color1 = colors.bottom().rgbStart();
                    m_color2 = colors.left().rgbEnd();

                    break;
                }
                case Qt::BottomRightCorner:
                {
                    m_color1 = colors.bottom().rgbStart();
                    m_color2 = colors.right().rgbEnd();

                    break;
                }
            }
        }

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
        QskVertex::Color m_color1, m_color2;
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

int QskRoundedRect::borderGradientLineCount( const QskBoxBorderColors& bc )
{
    return gradientLineCount( bc.left() ) + gradientLineCount( bc.top() )
        + gradientLineCount( bc.right() ) + gradientLineCount( bc.bottom() );
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

#if 1
        preferredOrientation = ( stepSizeSymmetries == Qt::Horizontal )
                ? Qt::Horizontal : Qt::Vertical;
#else
        preferredOrientation = ( stepSizeSymmetries == Qt::Vertical )
                ? Qt::Vertical : Qt::Horizontal;
#endif
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
                c.sx = -1.0;
                c.sy = -1.0;
                break;

            case TopRight:
                c.centerX = outerQuad.right - c.radiusX;
                c.centerY = outerQuad.top + c.radiusY;
                c.sx = +1.0;
                c.sy = -1.0;
                break;

            case BottomLeft:
                c.centerX = outerQuad.left + c.radiusX;
                c.centerY = outerQuad.bottom - c.radiusY;
                c.sx = -1.0;
                c.sy = +1.0;
                break;

            case BottomRight:
                c.centerX = outerQuad.right - c.radiusX;
                c.centerY = outerQuad.bottom - c.radiusY;
                c.sx = +1.0;
                c.sy = +1.0;
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

QskRoundedRect::BorderGeometryLayout::BorderGeometryLayout(
    const Metrics& metrics, const QskBoxBorderColors& colors )
{
    const struct
    {
        Qt::Corner corner;
        Qt::Edge edge;
    } order[4] =
    {
        // counter clockwise
        { Qt::BottomRightCorner, Qt::RightEdge },
        { Qt::TopRightCorner, Qt::TopEdge },
        { Qt::TopLeftCorner, Qt::LeftEdge },
        { Qt::BottomLeftCorner, Qt::BottomEdge }
    };

    /*
        In case of horizontal filling the lines end at right edge,
        while for vertical filling it is the bottom edge.
     */
    const int index0 = ( metrics.preferredOrientation == Qt::Horizontal ) ? 1 : 0;

    int pos = index0;
        
    for ( int i = 0; i < 4; i++ )
    {
        const int idx = ( index0 + i ) % 4;

        const auto corner = order[ idx ].corner;
        const auto edge = order[ idx ].edge;

        this->cornerOffsets[ corner ] = pos;
        pos += metrics.corner[ corner ].stepCount + 1;

        this->edgeOffsets[ ::edgeToIndex( edge ) ] = pos;
        pos += gradientLineCount( colors.gradientAt( edge ) );
    }

    if ( index0 == 0 )
    {
        this->closingOffsets[ 0 ] = 0;
        this->closingOffsets[ 1 ] = pos;
        this->lineCount = pos + 1;
    }
    else
    {
        pos--;

        this->closingOffsets[ 0 ] = pos;
        this->closingOffsets[ 1 ] = 0;
        this->lineCount = pos + 1;
    }
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

    const int stepCount = m_metrics.corner[ 0 ].stepCount;

    BorderValues v( m_metrics );

    const BorderGeometryLayout borderLayout( m_metrics, QskBoxBorderColors() );

    /*
        It would be possible to run over [0, 0.5 * M_PI_2]
        and create 8 values ( instead of 4 ) in each step. TODO ...
     */
    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        v.setAngle( it.cos(), it.sin() );

        const int j = it.step();
        const int k = it.stepCount() - it.step();

        for ( int i = 0; i < 4; i++ )
        {
            const auto corner = static_cast< Qt::Corner >( i );
            const auto& c = m_metrics.corner[ corner ];

            auto l = lines + borderLayout.cornerOffsets[ corner ];

            const bool inverted =
                ( corner == Qt::BottomLeftCorner || corner == Qt::TopRightCorner );

            l[ inverted ? k : j ].setLine(
                c.centerX + c.sx * v.dx1( corner ),
                c.centerY + c.sy * v.dy1( corner ),
                c.centerX + c.sx * v.dx2( corner ),
                c.centerY + c.sy * v.dy2( corner ) );
        }
    }

    lines[ borderLayout.closingOffsets[ 1 ] ]
        = lines[ borderLayout.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
        createRegularBox( borderLines, borderColors, fillLines, gradient );
    else
        createIrregularBox( borderLines, borderColors, fillLines, gradient );
}

void QskRoundedRect::Stroker::createRegularBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    Q_ASSERT( fillLines == nullptr || ( gradient.isValid() && gradient.stepCount() <= 1 ) );

    const QskVertex::ColorMap fillMap( gradient );

    const auto& c = m_metrics.corner;
    const int stepCount = c[ 0 ].stepCount;

    const BorderMaps borderMaps( stepCount, borderColors );

    QskVertex::ColoredLine* linesBR, * linesTR, * linesTL, * linesBL;
    linesBR = linesTR = linesTL = linesBL = nullptr;

    const int numCornerLines = stepCount + 1;
    const int numFillLines = fillLines ? 2 * numCornerLines : 0;

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
    {
        if ( borderLines )
        {
            linesTR = borderLines + 1;
            linesTL = linesTR + numCornerLines + gradientLineCount( borderColors.top() );
            linesBL = linesTL + numCornerLines + gradientLineCount( borderColors.left() );
            linesBR = linesBL + numCornerLines + gradientLineCount( borderColors.bottom() );
        }
    }
    else
    {
        if ( borderLines )
        {
            linesBR = borderLines;
            linesTR = linesBR + numCornerLines + gradientLineCount( borderColors.right() );
            linesTL = linesTR + numCornerLines + gradientLineCount( borderColors.top() );
            linesBL = linesTL + numCornerLines + gradientLineCount( borderColors.left() );
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
            const int k = stepCount - it.step();

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
            if ( m_metrics.preferredOrientation == Qt::Horizontal )
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

        const int k = 4 * numCornerLines + borderGradientLineCount( borderColors );

        if ( m_metrics.preferredOrientation == Qt::Horizontal )
            borderLines[ 0 ] = borderLines[ k ];
        else
            borderLines[ k ] = borderLines[ 0 ];
    }
}

void QskRoundedRect::Stroker::createIrregularBorder(
    QskVertex::ColoredLine* lines, const QskBoxBorderColors& colors ) const
{
    const BorderGeometryLayout borderLayout( m_metrics, colors );

    BorderValues v( m_metrics );

    for ( int i = 0; i < 4; i++ )
    {
        const auto corner = static_cast< Qt::Corner >( i );

        const auto& c = m_metrics.corner[ corner ];
        auto l = lines + borderLayout.cornerOffsets[ corner ];

        const BorderMap map( c.stepCount, colors, corner );

        const bool inverted =
            ( corner == Qt::BottomLeftCorner || corner == Qt::TopRightCorner ) ;

        for ( ArcIterator it( c.stepCount, inverted ); !it.isDone(); ++it )
        {
            const int index = inverted ? it.stepCount() - it.step() : it.step();

            v.setAngle( it.cos(), it.sin() );

            ( l++ )->setLine(
                c.centerX + c.sx * v.dx1(i), c.centerY + c.sy * v.dy1( i ),
                c.centerX + c.sx * v.dx2( i ), c.centerY + c.sy * v.dy2( i ),
                map.colorAt( index ) );
        }

        setBorderGradientLines( ::indexToEdge( i ), colors, 
            lines + borderLayout.edgeOffsets[ i ] );
    }

    lines[ borderLayout.closingOffsets[ 1 ] ]
        = lines[ borderLayout.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createIrregularFill(
    QskVertex::ColoredLine* lines, const QskGradient& gradient ) const
{
    const QskVertex::ColorMap fillMap( gradient );

    if ( m_metrics.isTotallyCropped )
    {
        const auto& q = m_metrics.innerQuad;

        fillMap.setLine( q.left, q.top, q.right, q.top, lines );
        fillMap.setLine( q.left, q.bottom, q.right, q.bottom, lines + 1 );

        return;
    }

    const auto& c = m_metrics.corner;

    BorderValues v( m_metrics );

    auto line = lines;

    int stepCount;

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
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
    n += borderGradientLineCount( colors );

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

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
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
