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
                pos += metrics.corners[ corner ].stepCount + 1;

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
    class CornerIterator : public ArcIterator
    {
      public:
        inline CornerIterator(
                const QskRoundedRect::Metrics& metrics,
                const QskBoxBorderColors& colors )
            : m_corners( metrics.corners )
            , m_maps{
                { m_corners[ Qt::TopLeftCorner ].stepCount,
                    colors.top().rgbStart(), colors.left().rgbEnd() },

                { m_corners[ Qt::TopRightCorner ].stepCount, 
                    colors.top().rgbEnd(), colors.right().rgbStart() },

                { m_corners[ Qt::BottomLeftCorner ].stepCount,
                    colors.bottom().rgbEnd(), colors.left().rgbStart() },

                { m_corners[ Qt::BottomRightCorner ].stepCount,
                    colors.bottom().rgbStart(), colors.right().rgbEnd() } }
        {
        }

        inline void setBorderLine( int corner, QskVertex::ColoredLine* line ) const
        {
            const auto color = m_maps[ corner ].colorAt( step() );
            m_corners[ corner ].setBorderLine( cos(), sin(), color, line );
        }

        inline void resetSteps( int corner, bool inverted = false )
        {
            reset( m_corners[ corner ].stepCount, inverted );
        }

      private:
        class Map
        {
          public:
            inline Map( int stepCount, const QskVertex::Color c1, QskVertex::Color c2 )
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

        const QskRoundedRect::Metrics::Corner* m_corners;
        const Map m_maps[4];
    };
}

namespace
{
    class LineMap
    {
      public:
        inline LineMap( const QskRoundedRect::Metrics& metrics )
            : m_corners( metrics.corners )
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
            , m_corners( metrics.corners )
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
        isRadiusRegular = shape.isRectellipse();

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
            auto& c = corners[ i ];

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

        centerQuad.left = qMax( corners[ TopLeft ].centerX,
            corners[ BottomLeft ].centerX );

        centerQuad.right = qMin( corners[ TopRight ].centerX,
            corners[ BottomRight ].centerX );

        centerQuad.top = qMax( corners[ TopLeft ].centerY,
            corners[ TopRight ].centerY );

        centerQuad.bottom = qMin( corners[ BottomLeft ].centerY,
            corners[ BottomRight ].centerY );

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
            auto& c = corners[ i ];

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
            corners[ TopLeft ].isCropped &&
            corners[ TopRight ].isCropped &&
            corners[ BottomRight ].isCropped &&
            corners[ BottomLeft ].isCropped;

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

    const auto cn = m_metrics.corners;

    qreal x1, x2, y1, y2;
    Qt::Orientation orientation;

    switch( edge )
    {
        case Qt::LeftEdge:
        {
            const auto& c1 = cn[ BottomLeft ];
            const auto& c2 = cn[ TopLeft ];

            orientation = Qt::Vertical;

            x1 = m_metrics.innerQuad.left;
            x2 = m_metrics.outerQuad.left;
            y1 = c1.isCropped ? c1.centerY + c1.radiusInnerY : c1.centerY;
            y2 = c2.isCropped ? c2.centerY - c2.radiusInnerY : c2.centerY;

            break;
        }
        case Qt::TopEdge:
        {
            const auto& c1 = cn[ TopLeft ];
            const auto& c2 = cn[ TopRight ];

            orientation = Qt::Horizontal;

            x1 = c1.isCropped ? c1.centerX - c1.radiusInnerX : c1.centerX;
            x2 = c2.isCropped ? c2.centerX + c2.radiusInnerX : c2.centerX;
            y1 = m_metrics.innerQuad.top;
            y2 = m_metrics.outerQuad.top;

            break;
        }
        case Qt::BottomEdge:
        {
            const auto& c1 = cn[ BottomRight ];
            const auto& c2 = cn[ BottomLeft ];

            orientation = Qt::Horizontal;

            x1 = c1.isCropped ? c1.centerX + c1.radiusInnerX : c1.centerX;
            x2 = c2.isCropped ? c2.centerX - c2.radiusInnerX : c2.centerX;
            y1 = m_metrics.innerQuad.bottom;
            y2 = m_metrics.outerQuad.bottom;

            break;
        }
        case Qt::RightEdge:
        {
            const auto& c1 = cn[ TopRight ];
            const auto& c2 = cn[ BottomRight ];

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
    const BorderGeometryLayout bl( m_metrics, QskBoxBorderColors() );
    const auto cn = m_metrics.corners;

    auto linesTL = lines + bl.cornerOffsets[ TopLeft ];
    auto linesTR = lines + bl.cornerOffsets[ TopRight ] + cn[ TopRight ].stepCount;
    auto linesBL = lines + bl.cornerOffsets[ BottomLeft ] + cn[ BottomLeft ].stepCount;
    auto linesBR = lines + bl.cornerOffsets[ BottomRight ];

    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
    {
        for ( ArcIterator it( cn[ 0 ].stepCount ); !it.isDone(); ++it )
        {
            cn[ TopLeft ].setBorderLine( it.cos(), it.sin(), linesTL++ );
            cn[ TopRight ].setBorderLine( it.cos(), it.sin(), linesTR-- );
            cn[ BottomLeft ].setBorderLine( it.cos(), it.sin(), linesBL-- );
            cn[ BottomRight ].setBorderLine( it.cos(), it.sin(), linesBR++ );
        }
    }
    else
    {
        for ( ArcIterator it( cn[ TopLeft ].stepCount ); !it.isDone(); ++it )
            cn[ TopLeft ].setBorderLine( it.cos(), it.sin(), linesTL++ );

        for ( ArcIterator it( cn[ TopRight ].stepCount ); !it.isDone(); ++it )
            cn[ TopRight ].setBorderLine( it.cos(), it.sin(), linesTR-- );

        for ( ArcIterator it( cn[ BottomLeft ].stepCount ); !it.isDone(); ++it )
            cn[ BottomLeft ].setBorderLine( it.cos(), it.sin(), linesBL-- );

        for ( ArcIterator it( cn[ BottomLeft ].stepCount ); !it.isDone(); ++it )
            cn[ BottomRight ].setBorderLine( it.cos(), it.sin(), linesBR++);
    }

    lines[ bl.closingOffsets[ 1 ] ] = lines[ bl.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createBorder(
    QskVertex::ColoredLine* lines, const QskBoxBorderColors& colors ) const
{
    Q_ASSERT( lines != nullptr );

    const BorderGeometryLayout bl( m_metrics, colors );
    const auto cn = m_metrics.corners;

    auto linesTL = lines + bl.cornerOffsets[ TopLeft ];
    auto linesTR = lines + bl.cornerOffsets[ TopRight ] + cn[ TopRight ].stepCount;
    auto linesBL = lines + bl.cornerOffsets[ BottomLeft ] + cn[ BottomLeft ].stepCount;
    auto linesBR = lines + bl.cornerOffsets[ BottomRight ];

    CornerIterator it( m_metrics, colors );

    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
    {
        for ( it.resetSteps( TopLeft ); !it.isDone(); ++it )
        {
            it.setBorderLine( TopLeft, linesTL++ );
            it.setBorderLine( TopRight, linesTR-- );
            it.setBorderLine( BottomLeft, linesBL-- );
            it.setBorderLine( BottomRight, linesBR++ );
        }
    }
    else
    {
        for ( it.resetSteps( TopLeft ); !it.isDone(); ++it )
            it.setBorderLine( TopLeft, linesTL++ );

        for ( it.resetSteps( TopRight ); !it.isDone(); ++it )
            it.setBorderLine( TopRight, linesTR-- );

        for ( it.resetSteps( BottomLeft ); !it.isDone(); ++it )
            it.setBorderLine( BottomLeft, linesBL-- );

        for ( it.resetSteps( BottomRight ); !it.isDone(); ++it )
            it.setBorderLine( BottomRight, linesBR++ );
    }

    for ( int i = 0; i < 4; i++ )
    {
        setBorderGradientLines( ::indexToEdge( i ), colors,
            lines + bl.edgeOffsets[ i ] );
    }

    lines[ bl.closingOffsets[ 1 ] ] = lines[ bl.closingOffsets[ 0 ] ];
}

template< class Line, class FillMap >
static inline void createFill(
    const QskRoundedRect::Metrics& m_metrics, const FillMap& map, Line* lines )
{
    using namespace QskRoundedRect;

    const auto cn = m_metrics.corners;

    if ( m_metrics.isTotallyCropped )
    {
        map.setHLine( TopLeft, TopRight, 0.0, 1.0, lines );
        map.setHLine( BottomLeft, BottomRight, 0.0, 1.0, lines + 1 );
    }
    else if ( m_metrics.isRadiusRegular )
    {
        const int stepCount = cn[ 0 ].stepCount;

        if ( m_metrics.preferredOrientation == Qt::Horizontal )
        {
            Line* l1 = lines + stepCount;
            Line* l2 = lines + stepCount + 1;

            for ( ArcIterator it( stepCount ); !it.isDone(); ++it )
            {
                map.setHLine( TopLeft, TopRight, it.cos(), it.sin(), l1++ );
                map.setHLine( BottomLeft, BottomRight, it.cos(), it.sin(), l2-- );
            }
        }
        else
        {
            Line* l1 = lines;
            Line* l2 = lines + 2 * stepCount + 1;

            for ( ArcIterator it( stepCount ); !it.isDone(); ++it )
            {
                map.setHLine( TopLeft, TopRight, it.cos(), it.sin(), l1++ );
                map.setHLine( BottomLeft, BottomRight, it.cos(), it.sin(), l2-- );
            }
        }
    }
    else
    {
        auto line = lines;

        if ( m_metrics.preferredOrientation == Qt::Horizontal )
        {
            int stepCount = qMax( cn[TopLeft].stepCount, cn[BottomLeft].stepCount );

            for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
                map.setVLine( TopLeft, BottomLeft, it.cos(), it.sin(), line++ );

            stepCount = qMax( cn[TopRight].stepCount, cn[BottomRight].stepCount );

            for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
                map.setVLine( TopRight, BottomRight, it.cos(), it.sin(), line++ );
        }
        else
        {
            int stepCount = qMax( cn[TopLeft].stepCount, cn[TopRight].stepCount );

            for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
                map.setHLine( TopLeft, TopRight, it.cos(), it.sin(), line++ );

            stepCount = qMax( cn[BottomLeft].stepCount, cn[BottomRight].stepCount );

            for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
                map.setHLine( BottomLeft, BottomRight, it.cos(), it.sin(), line++ );
        }
    }
}

void QskRoundedRect::Stroker::createFillLines( QskVertex::Line* lines ) const
{
    Q_ASSERT( lines );

    const LineMap map( m_metrics );
    ::createFill( m_metrics, map, lines );
}

void QskRoundedRect::Stroker::createFill(
    QskVertex::ColoredLine* lines, const QskGradient& gradient ) const
{
    Q_ASSERT( lines && ( gradient.isValid() && gradient.stepCount() <= 1 ) );

    const FillMap map( m_metrics, gradient );
    ::createFill( m_metrics, map, lines );
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
    const BorderGeometryLayout bl( m_metrics, borderColors );

    const FillMap fillMap( m_metrics, gradient );
    CornerIterator it( m_metrics, borderColors ); 

    /*
        It would be possible to run over [0, 0.5 * M_PI_2]
        and create 8 values ( instead of 4 ) in each step. TODO ...
     */

    const auto stepCount = m_metrics.corners[0].stepCount;

    auto linesTL = borderLines + bl.cornerOffsets[ TopLeft ];
    auto linesTR = borderLines + bl.cornerOffsets[ TopRight ] + stepCount;
    auto linesBL = borderLines + bl.cornerOffsets[ BottomLeft ] + stepCount;
    auto linesBR = borderLines + bl.cornerOffsets[ BottomRight ];

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
    {
        auto l1 = fillLines + stepCount;
        auto l2 = fillLines + stepCount + 1;

        for ( it.reset( stepCount ); !it.isDone(); ++it )
        {
            it.setBorderLine( TopLeft, linesTL++ );
            it.setBorderLine( TopRight, linesTR-- );
            it.setBorderLine( BottomLeft, linesBL-- );
            it.setBorderLine( BottomRight, linesBR++ );

            fillMap.setVLine( TopLeft, BottomLeft, it.cos(), it.sin(), l1-- );
            fillMap.setVLine( TopRight, BottomRight, it.cos(), it.sin(), l2++ );
        }
    }
    else
    {
        auto l1 = fillLines;
        auto l2 = fillLines + 2 * stepCount + 1;

        for ( it.reset( stepCount ); !it.isDone(); ++it )
        {
            it.setBorderLine( TopLeft, linesTL++ );
            it.setBorderLine( TopRight, linesTR-- );
            it.setBorderLine( BottomLeft, linesBL-- );
            it.setBorderLine( BottomRight, linesBR++ );

            fillMap.setHLine( TopLeft, TopRight, it.cos(), it.sin(), l1++ );
            fillMap.setHLine( BottomLeft, BottomRight, it.cos(), it.sin(), l2-- );
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

int QskRoundedRect::Stroker::borderLineCount() const
{
    if ( m_metrics.innerQuad == m_metrics.outerQuad )
        return 0;

    /*
        4: Number of lines is always one more than the number of steps.
        1: extra line at the end to close the border path
     */
    return m_metrics.cornerStepCount() + 4 + 1;
}

int QskRoundedRect::Stroker::borderLineCount( const QskBoxBorderColors& colors ) const
{
    int n = borderLineCount();

    if ( n > 0 )
    {
        n += gradientLineCount( colors.left() );
        n += gradientLineCount( colors.top() );
        n += gradientLineCount( colors.right() );
        n += gradientLineCount( colors.bottom() );
    }

    return n;
}

int QskRoundedRect::Stroker::fillLineCount() const
{
    if ( m_metrics.isTotallyCropped )
        return 2;

    const auto c = m_metrics.corners;

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

int QskRoundedRect::Stroker::fillLineCount( const QskGradient& gradient ) const
{
    if ( !gradient.isVisible() )
        return 0;

    return fillLineCount();
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

    const auto numPoints = m_metrics.cornerStepCount() + 4;

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
        const auto& c = m_metrics.corners[ corner ];

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
