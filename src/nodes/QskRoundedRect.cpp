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

    inline void setGradientLineAt(
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

    inline int edgeToIndex( Qt::Edge edge )
        { return qCountTrailingZeroBits( (quint8) edge ); }
}

namespace
{
    class CornerIterator : public ArcIterator
    {
      public:
        inline CornerIterator( const QskRoundedRect::Metrics& metrics )
            : m_corners( metrics.corners )
        {
        }

        inline void resetSteps( int corner, bool inverted = false )
        {
            reset( m_corners[ corner ].stepCount, inverted );
        }

        inline void setBorderLine( int corner, QskVertex::Line* line ) const
        {
            const auto& c = m_corners[ corner ];

            line->setLine( c.xInner( cos() ), c.yInner( sin() ),
                c.xOuter( cos() ), c.yOuter( sin() ) );
        }

      protected:
        const QskRoundedRect::Metrics::Corner* m_corners;
    };

    class CornerIteratorColor : public CornerIterator
    {
      public:
        inline CornerIteratorColor( const QskRoundedRect::Metrics& metrics,
                const QskBoxBorderColors& colors )
            : CornerIterator( metrics )
            , m_colors{
                { colors.top().rgbStart(), colors.left().rgbEnd() },
                { colors.top().rgbEnd(), colors.right().rgbStart() },
                { colors.bottom().rgbEnd(), colors.left().rgbStart() },
                { colors.bottom().rgbStart(), colors.right().rgbEnd() } }
        {
        }

        inline void setBorderLine( int corner, QskVertex::ColoredLine* line ) const
        {
            const auto& c = m_corners[ corner ];

            line->setLine( c.xInner( cos() ), c.yInner( sin() ),
                c.xOuter( cos() ), c.yOuter( sin() ), color( corner ) );
        }

      private:
        inline QskVertex::Color color( int corner ) const
        {
            const auto& cs = m_colors[ corner ];

            if ( cs.first == cs.second )
                return cs.first;

            const auto ratio = step() / qreal( m_corners[ corner ].stepCount );
            return cs.first.interpolatedTo( cs.second, ratio );
        }

        const QPair< QskVertex::Color, QskVertex::Color > m_colors[4];
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

        inline void setLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::Line* line ) const
        {
            const qreal x1 = m_corners[ corner1 ].xInner( cos );
            const qreal x2 = m_corners[ corner2 ].xInner( cos );

            const qreal y1 = m_corners[ corner1 ].yInner( sin );
            const qreal y2 = m_corners[ corner2 ].yInner( sin );

            line->setLine( x1, y1, x2, y2 );
        }

        const QskRoundedRect::Metrics::Corner* m_corners;
    };

    class FillMap
    {
      public:
        inline FillMap( const QskRoundedRect::Metrics& metrics,
                const QskGradient& gradient )
            : m_colorMap( gradient )
            , m_corners( metrics.corners )
        {
        }

        inline void setHLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::ColoredLine* line ) const
        {
            const qreal y = m_corners[ corner1 ].yInner( sin );

            const qreal x1 = m_corners[ corner1 ].xInner( cos );
            const qreal x2 = m_corners[ corner2 ].xInner( cos );

            m_colorMap.setLine( x1, y, x2, y, line );
        }

        inline void setVLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::ColoredLine* line ) const
        {
            const qreal x = m_corners[ corner1 ].xInner( cos );

            const qreal y1 = m_corners[ corner1 ].yInner( sin );
            const qreal y2 = m_corners[ corner2 ].yInner( sin );

            m_colorMap.setLine( x, y1, x, y2, line );
        }

        inline void setLine( int corner1, int corner2,
            qreal cos, qreal sin, QskVertex::ColoredLine* line ) const
        {
            const qreal x1 = m_corners[ corner1 ].xInner( cos );
            const qreal x2 = m_corners[ corner2 ].xInner( cos );

            const qreal y1 = m_corners[ corner1 ].yInner( sin );
            const qreal y2 = m_corners[ corner2 ].yInner( sin );

            m_colorMap.setLine( x1, y1, x2, y2, line );
        }

        const QskVertex::ColorMap m_colorMap;
        const QskRoundedRect::Metrics::Corner* m_corners;
    };
}

namespace QskRoundedRect
{
    GeometryLayout::GeometryLayout(
        const QskRoundedRect::Metrics& metrics, const QskBoxBorderColors& colors )
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

        if ( stepSizeSymmetries == Qt::Horizontal )
        {
            preferredOrientation = Qt::Horizontal;
        }
        else if ( stepSizeSymmetries == Qt::Vertical )
        {
            preferredOrientation = Qt::Vertical;
        }
        else
        {
            const auto tl = corners[ TopLeft ].stepCount;
            const auto tr = corners[ TopRight ].stepCount;
            const auto bl = corners[ BottomLeft ].stepCount;
            const auto br = corners[ BottomRight ].stepCount;

            if ( qMax( tl, tr ) + qMax( bl, br ) >= qMax( tl, bl ) + qMax( tr, br ) )
                preferredOrientation = Qt::Vertical;
            else
                preferredOrientation = Qt::Horizontal;
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

QskRoundedRect::Stroker::Stroker( const Metrics& metrics )
    : m_metrics( metrics )
    , m_geometryLayout( metrics, QskBoxBorderColors() )
    , m_isColored( false )
{
}

QskRoundedRect::Stroker::Stroker( const Metrics& metrics,
        const QskBoxBorderColors& borderColors, const QskGradient& gradient )
    : m_metrics( metrics )
    , m_borderColors( borderColors )
    , m_gradient( gradient )
    , m_geometryLayout( metrics, m_borderColors )
    , m_isColored( true )
{
}

void QskRoundedRect::Stroker::setBorderGradientLines(
    QskVertex::ColoredLine* lines ) const
{
    const auto off= m_geometryLayout.edgeOffsets;

    setBorderGradientLines( Qt::TopEdge, lines + off[0] );
    setBorderGradientLines( Qt::LeftEdge, lines + off[1] );
    setBorderGradientLines( Qt::RightEdge, lines + off[2] );
    setBorderGradientLines( Qt::BottomEdge, lines + off[3] );
}

void QskRoundedRect::Stroker::setBorderGradientLines(
    Qt::Edge edge, QskVertex::ColoredLine* lines ) const
{
    const auto& gradient = m_borderColors.gradientAt( edge );
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
            orientation = Qt::Vertical;

            x1 = m_metrics.innerQuad.left;
            x2 = m_metrics.outerQuad.left;
            y1 = cn[ BottomLeft ].yInner( 0.0 );
            y2 = cn[ TopLeft ].yInner( 0.0 );

            break;
        }
        case Qt::TopEdge:
        {
            orientation = Qt::Horizontal;

            x1 = cn[ TopLeft ].xInner( 0.0 );
            x2 = cn[ TopRight ].xInner( 0.0 ) ;
            y1 = m_metrics.innerQuad.top;
            y2 = m_metrics.outerQuad.top;

            break;
        }
        case Qt::BottomEdge:
        {
            orientation = Qt::Horizontal;

            x1 = cn[ BottomRight ].xInner( 0.0 );
            x2 = cn[ BottomLeft ].xInner( 0.0 );
            y1 = m_metrics.innerQuad.bottom;
            y2 = m_metrics.outerQuad.bottom;

            break;
        }
        case Qt::RightEdge:
        {
            orientation = Qt::Vertical;

            x1 = m_metrics.innerQuad.right;
            x2 = m_metrics.outerQuad.right;
            y1 = cn[ TopRight ].yInner( 0.0 );
            y2 = cn[ BottomRight ].yInner( 0.0 );

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
    Q_ASSERT( !m_isColored );

    const auto& gl = m_geometryLayout;
    const auto cn = m_metrics.corners;

    auto linesTL = lines + gl.cornerOffsets[ TopLeft ];
    auto linesTR = lines + gl.cornerOffsets[ TopRight ] + cn[ TopRight ].stepCount;
    auto linesBL = lines + gl.cornerOffsets[ BottomLeft ] + cn[ BottomLeft ].stepCount;
    auto linesBR = lines + gl.cornerOffsets[ BottomRight ];

    CornerIterator it( m_metrics );

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

        for ( it.resetSteps( BottomLeft ); !it.isDone(); ++it )
            it.setBorderLine( BottomLeft, linesBR++);
    }

    lines[ gl.closingOffsets[ 1 ] ] = lines[ gl.closingOffsets[ 0 ] ];
}

void QskRoundedRect::Stroker::createBorder( QskVertex::ColoredLine* lines ) const
{
    Q_ASSERT( m_isColored );
    Q_ASSERT( lines );

    const auto& gl = m_geometryLayout;
    const auto cn = m_metrics.corners;

    auto linesTL = lines + gl.cornerOffsets[ TopLeft ];
    auto linesTR = lines + gl.cornerOffsets[ TopRight ] + cn[ TopRight ].stepCount;
    auto linesBL = lines + gl.cornerOffsets[ BottomLeft ] + cn[ BottomLeft ].stepCount;
    auto linesBR = lines + gl.cornerOffsets[ BottomRight ];

    CornerIteratorColor it( m_metrics, m_borderColors );

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

    setBorderGradientLines( lines );
    lines[ gl.closingOffsets[ 1 ] ] = lines[ gl.closingOffsets[ 0 ] ];
}

template< class Line, class FillMap >
static inline void createFill(
    const QskRoundedRect::Metrics& m_metrics, const FillMap& map, Line* lines )
{
    using namespace QskRoundedRect;

    const auto cn = m_metrics.corners;
    const bool isHorizontal = m_metrics.preferredOrientation == Qt::Horizontal;

    if ( m_metrics.isTotallyCropped )
    {
        map.setHLine( TopLeft, TopRight, 0.0, 1.0, lines );
        map.setHLine( BottomLeft, BottomRight, 0.0, 1.0, lines + 1 );
    }
    else if ( m_metrics.isRadiusRegular )
    {
        const int stepCount = cn[ 0 ].stepCount;

        if ( isHorizontal )
        {
            Line* l1 = lines + stepCount;
            Line* l2 = lines + stepCount + 1;

            for ( ArcIterator it( stepCount ); !it.isDone(); ++it )
            {
                map.setVLine( TopLeft, BottomLeft, it.cos(), it.sin(), l1++ );
                map.setVLine( TopRight, BottomRight, it.cos(), it.sin(), l2-- );
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
    else if ( m_metrics.stepSizeSymmetries )
    {

        auto line = lines;

        if ( isHorizontal )
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
    else
    {
        /*
            This fallback code creates the same points. The cases above are
            simply micro oprimization reducing the loops or calculations
            to get there.
         */

        auto line = lines;

        if ( isHorizontal )
        {
            int stepCount = qMax( cn[TopLeft].stepCount, cn[BottomLeft].stepCount );

            for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
                map.setLine( TopLeft, BottomLeft, it.cos(), it.sin(), line++ );

            stepCount = qMax( cn[TopRight].stepCount, cn[BottomRight].stepCount );

            for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
                map.setLine( TopRight, BottomRight, it.cos(), it.sin(), line++ );
        }
        else
        {
            int stepCount = qMax( cn[TopLeft].stepCount, cn[TopRight].stepCount );

            for ( ArcIterator it( stepCount, false ); !it.isDone(); ++it )
                map.setLine( TopLeft, TopRight, it.cos(), it.sin(), line++ );

            stepCount = qMax( cn[BottomLeft].stepCount, cn[BottomRight].stepCount );

            for ( ArcIterator it( stepCount, true ); !it.isDone(); ++it )
                map.setLine( BottomLeft, BottomRight, it.cos(), it.sin(), line++ );
        }
    }
}

void QskRoundedRect::Stroker::createFillLines( QskVertex::Line* lines ) const
{
    Q_ASSERT( !m_isColored );
    Q_ASSERT( lines );

    const LineMap map( m_metrics );
    ::createFill( m_metrics, map, lines );
}

void QskRoundedRect::Stroker::createFill( QskVertex::ColoredLine* lines ) const
{
    Q_ASSERT( m_isColored );
    Q_ASSERT( lines );
    Q_ASSERT( lines && ( m_gradient.isValid() && m_gradient.stepCount() <= 1 ) );

    const FillMap map( m_metrics, m_gradient );
    ::createFill( m_metrics, map, lines );
}

void QskRoundedRect::Stroker::createBox( QskVertex::ColoredLine* borderLines, 
    QskVertex::ColoredLine* fillLines ) const
{
    Q_ASSERT( m_isColored );
    Q_ASSERT( borderLines || fillLines );
    Q_ASSERT( fillLines == nullptr || ( m_gradient.isValid() && m_gradient.stepCount() <= 1 ) );

    if ( m_metrics.isRadiusRegular && !m_metrics.isTotallyCropped )
    {
        if ( borderLines && fillLines )
        {
            /*
                Doing all in one allows a slightly faster implementation.
                As this is the by far most common situation we do this
                micro optimization.
             */
            createRegularBox( borderLines, fillLines );
            return;
        }
    }

    if ( borderLines )
        createBorder( borderLines );

    if ( fillLines )
        createFill( fillLines );
}

void QskRoundedRect::Stroker::createRegularBox(
    QskVertex::ColoredLine* borderLines, QskVertex::ColoredLine* fillLines ) const
{
    const auto& gl = m_geometryLayout;

    const FillMap fillMap( m_metrics, m_gradient );
    CornerIteratorColor it( m_metrics, m_borderColors );

    /*
        It would be possible to run over [0, 0.5 * M_PI_2]
        and create 8 values ( instead of 4 ) in each step. TODO ...
     */

    const auto stepCount = m_metrics.corners[0].stepCount;

    auto linesTL = borderLines + gl.cornerOffsets[ TopLeft ];
    auto linesTR = borderLines + gl.cornerOffsets[ TopRight ] + stepCount;
    auto linesBL = borderLines + gl.cornerOffsets[ BottomLeft ] + stepCount;
    auto linesBR = borderLines + gl.cornerOffsets[ BottomRight ];

    if ( m_metrics.preferredOrientation == Qt::Horizontal )
    {
        auto l1 = fillLines + stepCount;
        auto l2 = fillLines + stepCount + 1;

        for ( it.resetSteps( TopLeft ); !it.isDone(); ++it )
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

        for ( it.resetSteps( TopLeft ); !it.isDone(); ++it )
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
        setBorderGradientLines( borderLines );
        borderLines[ gl.closingOffsets[ 1 ] ] = borderLines[ gl.closingOffsets[ 0 ] ];
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
    int n = m_metrics.cornerStepCount() + 4 + 1;

    if ( m_isColored )
    {
        n += gradientLineCount( m_borderColors.left() );
        n += gradientLineCount( m_borderColors.top() );
        n += gradientLineCount( m_borderColors.right() );
        n += gradientLineCount( m_borderColors.bottom() );
    }

    return n;
}

int QskRoundedRect::Stroker::fillLineCount() const
{
    if ( m_isColored && !m_gradient.isVisible() )
        return 0;

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
