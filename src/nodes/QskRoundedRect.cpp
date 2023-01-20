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
        // only the intermediate gradient lines !
        return qMax( 0, borderGradient.stepCount() - 1 );
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
}

namespace
{
    inline int edgeToIndex( Qt::Edge edge ) { return qCountTrailingZeroBits( (quint8) edge ); }
    inline Qt::Edge indexToEdge( int index ) { return static_cast< Qt::Edge >( 1 << index ); }

    class BorderGeometryLayout
    {
      public:
        BorderGeometryLayout( const QskRoundedRect::Metrics& metrics,
            const QskBoxBorderColors& colors )
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

        int cornerOffsets[ 4 ];
        int edgeOffsets[ 4 ];

        int closingOffsets[2];
        int lineCount;
    };
}

namespace
{
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

        inline BorderMap( int stepCount, const QskVertex::Color c1, QskVertex::Color c2 )
            : m_stepCount( stepCount )
            , m_color1( c1 )
            , m_color2( c2 )
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
                { stepCount, colors.top().rgbStart(), colors.left().rgbEnd() },
                { stepCount, colors.top().rgbEnd(), colors.right().rgbStart() },
                { stepCount, colors.bottom().rgbEnd(), colors.left().rgbStart() },
                { stepCount, colors.bottom().rgbStart(), colors.right().rgbEnd() } }
        {
        }

        const BorderMap maps[4];
    };
}

namespace
{
    class LineMap
    {
      public:
        inline LineMap( const QskRoundedRect::Metrics& metrics )
            : m_corners( metrics.corner )
        {
        }

        inline void setHLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::Line* line ) const
        {
            const qreal y = m_corners[ corner1 ].yInner( sin );

            const qreal x1 = m_corners[ corner1 ].xInner( cos );
            const qreal x2 = m_corners[ corner2 ].xInner( cos );

            line->setLine( x1, y, x2, y );
        }

        inline void setVLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::Line* line ) const
        {
            const qreal x = m_corners[ corner1 ].xInner( cos );

            const qreal y1 = m_corners[ corner1 ].yInner( sin );
            const qreal y2 = m_corners[ corner2 ].yInner( sin );

            line->setLine( x, y1, x, y2 );
        }

        const QskRoundedRect::Metrics::Corner* m_corners;
    };

    class FillMap : public QskVertex::ColorMap
    {
      public:
        inline FillMap( const QskRoundedRect::Metrics& metrics,
                const QskGradient& gradient )
            : ColorMap( gradient )
            , m_corners( metrics.corner )
        {
        }

        inline void setHLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::ColoredLine* line ) const
        {
            const qreal y = m_corners[ corner1 ].yInner( sin );

            const qreal x1 = m_corners[ corner1 ].xInner( cos );
            const qreal x2 = m_corners[ corner2 ].xInner( cos );

            setLine( x1, y, x2, y, line );
        }

        inline void setVLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::ColoredLine* line ) const
        {
            const qreal x = m_corners[ corner1 ].xInner( cos );

            const qreal y1 = m_corners[ corner1 ].yInner( sin );
            const qreal y2 = m_corners[ corner2 ].yInner( sin );

            setLine( x, y1, x, y2, line );
        }

        const QskRoundedRect::Metrics::Corner* m_corners;
    };
}

namespace QskRoundedRect
{

    Metrics::Metrics( const QRectF& rect,
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

            preferredOrientation = ( stepSizeSymmetries == Qt::Horizontal )
                    ? Qt::Horizontal : Qt::Vertical;
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

            if ( c.radiusInnerX >= 0.0 )
            {
                c.x0 = 0.0;
                c.rx = c.radiusInnerX;
            }
            else
            {
                c.x0 = c.radiusInnerX;
                c.rx = 0.0;
            }

            if ( c.radiusInnerY >= 0.0 )
            {
                c.y0 = 0.0;
                c.ry = c.radiusInnerY;
            }
            else
            {
                c.y0 = c.radiusInnerY;
                c.ry = 0.0;
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
    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
        createRegularBorderLines( lines );
    else
        createIrregularBorderLines( lines );
}

void QskRoundedRect::Stroker::createRegularBorderLines( QskVertex::Line* lines ) const
{
    const auto c = m_metrics.corner;

    const int stepCount = c[ 0 ].stepCount;

    const BorderGeometryLayout borderLayout( m_metrics, QskBoxBorderColors() );

    const auto index = borderLayout.cornerOffsets;

    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        const int j = it.step();
        const int k = it.stepCount() - it.step();

        const auto cos = it.cos();
        const auto sin = it.sin();

        c[ TopLeft ].setBorderLine( cos, sin, lines + index[ TopLeft ] + j );
        c[ BottomLeft ].setBorderLine( cos, sin, lines + index[ BottomLeft ] + k );
        c[ BottomRight ].setBorderLine( cos, sin, lines + index[ BottomRight ] + j );
        c[ TopRight ].setBorderLine( cos, sin, lines + index[ TopRight ] + k );
    }

    lines[ borderLayout.closingOffsets[ 1 ] ]
        = lines[ borderLayout.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createIrregularBorderLines( QskVertex::Line* lines ) const
{
    const BorderGeometryLayout borderLayout( m_metrics, QskBoxBorderColors() );

    for ( int i = 0; i < 4; i++ )
    {
        const auto corner = static_cast< Qt::Corner >( i );

        const auto& c = m_metrics.corner[ corner ];
        auto l = lines + borderLayout.cornerOffsets[ corner ];

        const bool inverted =
            ( corner == Qt::BottomLeftCorner || corner == Qt::TopRightCorner );

        for ( ArcIterator it( c.stepCount, inverted ); !it.isDone(); ++it )
            c.setBorderLine( it.cos(), it.sin(), l++ );
    }

    lines[ borderLayout.closingOffsets[ 1 ] ]
        = lines[ borderLayout.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createBorder(
    QskVertex::ColoredLine* lines, const QskBoxBorderColors& colors ) const
{
    Q_ASSERT( lines != nullptr );

    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
        createRegularBorder( lines, colors );
    else
        createIrregularBorder( lines, colors );
}

void QskRoundedRect::Stroker::createRegularBorder(
    QskVertex::ColoredLine* lines, const QskBoxBorderColors& colors ) const
{
    const int stepCount = m_metrics.corner[ 0 ].stepCount;

    const BorderMaps bm( stepCount, colors );
    const BorderGeometryLayout bl( m_metrics, colors );

    const auto maps = bm.maps;

    auto linesTL = lines + bl.cornerOffsets[ TopLeft ];
    auto linesTR = lines + bl.cornerOffsets[ TopRight ] + stepCount;
    auto linesBL = lines + bl.cornerOffsets[ BottomLeft ] + stepCount;
    auto linesBR = lines + bl.cornerOffsets[ BottomRight ];

    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        const qreal cos = it.cos();
        const qreal sin = it.sin();
        const int i =  it.step();

        m_metrics.corner[ TopLeft ].setBorderLine(
            cos, sin, maps[ TopLeft ].colorAt( i ), linesTL++ );

        m_metrics.corner[ TopRight ].setBorderLine(
            cos, sin, maps[ TopRight ].colorAt( i ), linesTR-- );

        m_metrics.corner[ BottomLeft ].setBorderLine(
            cos, sin, maps[ BottomLeft ].colorAt( i ), linesBL-- );

        m_metrics.corner[ BottomRight ].setBorderLine(
            cos, sin, maps[ BottomRight ].colorAt( i ), linesBR++ );
    }

    for ( int i = 0; i < 4; i++ )
    {
        setBorderGradientLines( ::indexToEdge( i ), colors,
            lines + bl.edgeOffsets[ i ] );
    }

    lines[ bl.closingOffsets[ 1 ] ] = lines[ bl.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createIrregularBorder(
    QskVertex::ColoredLine* lines, const QskBoxBorderColors& colors ) const
{
    const BorderGeometryLayout borderLayout( m_metrics, colors );

    for ( int i = 0; i < 4; i++ )
    {
        const auto corner = static_cast< Qt::Corner >( i );

        const auto& c = m_metrics.corner[ corner ];
        auto l = lines + borderLayout.cornerOffsets[ corner ];

        const BorderMap map( c.stepCount, colors, corner );

        const bool inverted =
            ( corner == Qt::BottomLeftCorner || corner == Qt::TopRightCorner );

        for ( ArcIterator it( c.stepCount, inverted ); !it.isDone(); ++it )
        {
            const int index = inverted ? it.stepCount() - it.step() : it.step();
            c.setBorderLine( it.cos(), it.sin(), map.colorAt( index ), l++ );
        }

        setBorderGradientLines( ::indexToEdge( i ), colors,
            lines + borderLayout.edgeOffsets[ i ] );
    }

    lines[ borderLayout.closingOffsets[ 1 ] ]
        = lines[ borderLayout.closingOffsets[ 0 ] ];
}

template< class Line, class FillMap >
static inline void createIrregularFill(
    const QskRoundedRect::Metrics& m_metrics, const FillMap& map, Line* lines )
{
    using namespace QskRoundedRect;

    const auto& c = m_metrics.corner;

    auto line = lines;

    int stepCount;

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
    {
        stepCount = qMax( c[TopLeft].stepCount, c[BottomLeft].stepCount );

        for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
            map.setVLine( TopLeft, BottomLeft, it.cos(), it.sin(), line++ );

        stepCount = qMax( c[TopRight].stepCount, c[BottomRight].stepCount );

        for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
            map.setVLine( TopRight, BottomRight, it.cos(), it.sin(), line++ );
    }
    else
    {
        stepCount = qMax( c[TopLeft].stepCount, c[TopRight].stepCount );

        for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
            map.setHLine( TopLeft, TopRight, it.cos(), it.sin(), line++ );

        stepCount = qMax( c[BottomLeft].stepCount, c[BottomRight].stepCount );

        for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
            map.setHLine( BottomLeft, BottomRight, it.cos(), it.sin(), line++ );
    }
}

template< class Line, class FillMap >
static inline void createRegularFill(
    const QskRoundedRect::Metrics& m_metrics, const FillMap& map, Line* lines )
{
    using namespace QskRoundedRect;

    const int stepCount = m_metrics.corner[ 0 ].stepCount;

    Line* l1, *l2;

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
    {
        l1 = lines + stepCount;
        l2 = lines + stepCount + 1;
    }
    else
    {
        l1 = lines;
        l2 = lines + 2 * stepCount + 1;
    }

    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        const auto cos = it.cos();
        const auto sin = it.sin();

        if ( m_metrics.preferredOrientation == Qt::Horizontal )
        {
            map.setVLine( TopLeft, BottomLeft, cos, sin, l1-- );
            map.setVLine( TopRight, BottomRight, cos, sin, l2++ );
        }
        else
        {
            map.setHLine( TopLeft, TopRight, cos, sin, l1++ );
            map.setHLine( BottomLeft, BottomRight, cos, sin, l2-- );
        }
    }
}

void QskRoundedRect::Stroker::createFillLines( QskVertex::Line* lines ) const
{
    if ( m_metrics.isTotallyCropped )
    {
        const auto& q = m_metrics.innerQuad;

        lines[0].setLine( q.left, q.top, q.right, q.top );
        lines[1].setLine( q.left, q.bottom, q.right, q.bottom );
    }
    else if ( m_metrics.isRadiusRegular )
    {
        const LineMap map( m_metrics );
        ::createRegularFill( m_metrics, map, lines );
    }
    else
    {
        const LineMap map( m_metrics );
        ::createIrregularFill( m_metrics, map, lines );
    }
}

void QskRoundedRect::Stroker::createFill(
    QskVertex::ColoredLine* lines, const QskGradient& gradient ) const
{
    Q_ASSERT( lines && ( gradient.isValid() && gradient.stepCount() <= 1 ) );

    const FillMap map( m_metrics, gradient );

    if ( m_metrics.isTotallyCropped )
    {
        const auto& q = m_metrics.innerQuad;

        map.setLine( q.left, q.top, q.right, q.top, lines );
        map.setLine( q.left, q.bottom, q.right, q.bottom, lines + 1 );
    }
    else if ( m_metrics.isRadiusRegular )
    {
        ::createRegularFill( m_metrics, map, lines );
    }
    else
    {
        ::createIrregularFill( m_metrics, map, lines );
    }
}

void QskRoundedRect::Stroker::createBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    Q_ASSERT( borderLines || fillLines );
    Q_ASSERT( fillLines == nullptr || ( gradient.isValid() && gradient.stepCount() <= 1 ) );

    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
    {
        if ( borderLines && fillLines )
        {
            /*
                Doing all in one allows a slightly faster implementation.
                As this is the by far most common situation we do this
                micro optimization.
             */
            createRegularBox( borderLines, borderColors, fillLines, gradient );
            return;
        }
    }

    if ( borderLines )
        createBorder( borderLines, borderColors );

    if ( fillLines )
        createFill( fillLines, gradient );
}

void QskRoundedRect::Stroker::createRegularBox(
    QskVertex::ColoredLine* borderLines, const QskBoxBorderColors& borderColors,
    QskVertex::ColoredLine* fillLines, const QskGradient& gradient ) const
{
    const int stepCount = m_metrics.corner[ 0 ].stepCount;

    const BorderMaps borderMaps( stepCount, borderColors );
    const BorderGeometryLayout bl( m_metrics, borderColors );

    const FillMap fillMap( m_metrics, gradient );

    /*
        It would be possible to run over [0, 0.5 * M_PI_2]
        and create 8 values ( instead of 4 ) in each step. TODO ...
     */

    QskVertex::ColoredLine* l1, *l2;

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
    {
        l1 = fillLines + stepCount;
        l2 = fillLines + stepCount + 1;
    }
    else
    {
        l1 = fillLines;
        l2 = fillLines + 2 * stepCount + 1;
    }

    auto linesTL = borderLines + bl.cornerOffsets[ TopLeft ];
    auto linesTR = borderLines + bl.cornerOffsets[ TopRight ] + stepCount;
    auto linesBL = borderLines + bl.cornerOffsets[ BottomLeft ] + stepCount;
    auto linesBR = borderLines + bl.cornerOffsets[ BottomRight ];

    const auto maps = borderMaps.maps;

    for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
    {
        const qreal cos = it.cos();
        const qreal sin = it.sin();
        const int i = it.step();

        m_metrics.corner[ TopLeft ].setBorderLine(
            cos, sin, maps[ TopLeft ].colorAt( i ), linesTL++ );

        m_metrics.corner[ TopRight ].setBorderLine(
            cos, sin, maps[ TopRight ].colorAt( i ), linesTR-- );

        m_metrics.corner[ BottomLeft ].setBorderLine(
            cos, sin, maps[ BottomLeft ].colorAt( i ), linesBL-- );

        m_metrics.corner[ BottomRight ].setBorderLine(
            cos, sin, maps[ BottomRight ].colorAt( i ), linesBR++ );

        if ( m_metrics.preferredOrientation == Qt::Horizontal )
        {
            fillMap.setVLine( TopLeft, BottomLeft, cos, sin, l1-- );
            fillMap.setVLine( TopRight, BottomRight, cos, sin, l2++ );
        }
        else
        {
            fillMap.setHLine( TopLeft, TopRight, cos, sin, l1++ );
            fillMap.setHLine( BottomLeft, BottomRight, cos, sin, l2-- );
        }
    }

    if ( borderLines )
    {
        for ( int i = 0; i < 4; i++ )
        {
            setBorderGradientLines( ::indexToEdge( i ), borderColors,
                borderLines + bl.edgeOffsets[ i ] );
        }

        borderLines[ bl.closingOffsets[ 1 ] ]
            = borderLines[ bl.closingOffsets[ 0 ] ];
    }
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

    n += gradientLineCount( colors.left() );
    n += gradientLineCount( colors.top() );
    n += gradientLineCount( colors.right() );
    n += gradientLineCount( colors.bottom() );

    n++; // duplicating the first line at the end to close the border path

    return n;
}

int QskRoundedRect::Stroker::fillLineCount( const QskGradient& gradient ) const
{
    if ( !gradient.isVisible() )
        return 0;

    return fillLineCount();
}

int QskRoundedRect::Stroker::fillLineCount() const
{
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

void QskRoundedRect::Stroker::createFillFanLines( QSGGeometry& geometry )
{
    if ( ( m_metrics.innerQuad.width <= 0 ) || ( m_metrics.innerQuad.height <= 0 ) )
    {
        geometry.allocate( 0 );
        return;
    }

    const auto& in = m_metrics.innerQuad;

    if ( m_metrics.isTotallyCropped )
    {
        // degenerated to a rectangle

        geometry.allocate( 4 );

        auto p = geometry.vertexDataAsPoint2D();
        p[0].set( in.left, in.top );
        p[1].set( in.right, in.top );
        p[2].set( in.left, in.bottom );
        p[3].set( in.right, in.bottom );

        return;
    }

    /*
        Unfortunately QSGGeometry::DrawTriangleFan is no longer supported with
        Qt6 and we have to go with DrawTriangleStrip, duplicating the center with
        each vertex.

        However we need less memory, when using Horizontal/Vertical lines
        instead of the fan. TODO ...
     */

    const auto numPoints =
            m_metrics.corner[0].stepCount
            + m_metrics.corner[1].stepCount
            + m_metrics.corner[2].stepCount
            + m_metrics.corner[3].stepCount + 4;

    /*
        points: center point + interpolated corner points
        indexes: lines between the center and each point, where
                 the first line needs to be appended to close the filling
     */

    geometry.allocate( 1 + numPoints, 2 * ( numPoints + 1 ) );

    Q_ASSERT( geometry.sizeOfIndex() == 2 );

    auto points = geometry.vertexDataAsPoint2D();
    auto indexes = geometry.indexDataAsUShort();

    int i = 0;

    points[i++].set( in.left + 0.5 * in.width, in.top + 0.5 * in.height );

    bool inverted = false;

    for ( const auto corner : { TopLeft, BottomLeft, BottomRight, TopRight } )
    {
        const auto& c = m_metrics.corner[ corner ];

        for ( ArcIterator it( c.stepCount, inverted ); !it.isDone(); ++it )
        {
            *indexes++ = 0;
            *indexes++ = i;

            points[i++].set( c.xInner( it.cos() ), c.yInner( it.sin() ) );
        }

        inverted = !inverted;
    }

    *indexes++ = 0;
    *indexes++ = 1;
}
