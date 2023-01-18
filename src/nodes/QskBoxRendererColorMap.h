/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#ifndef QSK_BOX_RENDERER_COLOR_MAP_H
#define QSK_BOX_RENDERER_COLOR_MAP_H

#include <QskGradient.h>
#include <QskGradientDirection.h>
#include <QskVertex.h>

#include <cassert>

class QskBoxShapeMetrics;

namespace QskVertex
{
    inline bool gradientLinesNeeded( const QRectF& rect, const QskGradient& gradient )
    {
        if ( gradient.isMonochrome() )
            return false;

        switch( gradient.stepCount() )
        {
            case 0:
                return false;

            case 1:
            {
                Q_ASSERT( gradient.stretchMode() != QskGradient::StretchToSize );
                return !gradient.linearDirection().contains( rect );
            }

            default:
                return true;
        }
    }

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

        inline void setLine( qreal x1, qreal y1, qreal x2, qreal y2,
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
        inline QskVertex::Color colorAt( qreal x, qreal y ) const
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

    class ColorIterator
    {
      public:
        static inline bool advance()
        {
            return false;
        }

        inline qreal value() const
        {
            assert( false );
            return 0.0;
        }

        inline Color color() const
        {
            assert( false );
            return Color();
        }

        static inline bool isDone()
        {
            return true;
        }
    };

    class SimpleColorIterator : public ColorIterator
    {
      public:
        inline SimpleColorIterator( const QColor& color )
            : m_color1( color )
            , m_color2( color )
            , m_isMonochrome( true )
        {
        }

        inline SimpleColorIterator( const QColor& color1, const QColor& color2 )
            : m_color1( color1 )
            , m_color2( color2 )
            , m_isMonochrome( false )
        {
        }

        inline Color colorAt( qreal value ) const
        {
            if ( m_isMonochrome )
                return m_color1;

            return m_color1.interpolatedTo( m_color2, value );
        }

      private:
        const Color m_color1, m_color2;
        const bool m_isMonochrome;
    };

    class GradientColorIterator : public ColorIterator
    {
      public:
        inline GradientColorIterator( const QskGradientStops& stops )
            : m_stops( stops )
            , m_index( 0 )
        {
            const auto& s = stops[0];

            m_pos1 = m_pos2 = s.position();
            m_color1 = m_color2 = s.rgb();
        }

        inline qreal value() const { return m_pos2; }
        inline Color color() const { return m_color2; }

        inline Color colorAt( qreal value ) const
        {
            const auto t = m_pos2 - m_pos1;

            const auto ratio = ( t == 0.0 ) ? 0.0 : ( value - m_pos1 ) / t;
            return m_color1.interpolatedTo( m_color2, ratio );
        }

        inline bool advance()
        {
            m_pos1 = m_pos2;
            m_color1 = m_color2;

            if ( ++m_index < m_stops.size() )
            {
                const auto& s = m_stops[ m_index ];

                m_pos2 = s.position();
                m_color2 = s.rgb();
            }

            return !isDone();
        }

        inline bool isDone() const { return m_index >= m_stops.size(); }

      private:
        const QskGradientStops m_stops;

        int m_index;

        Color m_color1, m_color2;
        qreal m_pos1, m_pos2;
    };

    inline ColoredLine* fillUp( ColoredLine* lines, const ColoredLine& l, int count )
    {
        for ( int i = 0; i < count; i++ )
            *lines++ = l;

        return lines;
    }

    template< class ContourIterator, class ColorIterator >
    ColoredLine* fillOrdered( ContourIterator& contourIt,
        ColorIterator& colorIt, int lineCount, ColoredLine* lines )
    {
        /*
             When the the vector exceeds [ 0.0, 1.0 ] we might have
             gradient lines lying outside the contour.
             This effect could be precalculated - however we might end
             up difficult code with potential bugs.

             So we allow the allocation code to ignore the effect by
             adding duplicates of the last line.
         */

        const auto value1 = contourIt.valueBegin();
        const auto value2 = contourIt.valueEnd();

        ColoredLine* l = lines;

        do
        {
            while ( !colorIt.isDone() && ( colorIt.value() < contourIt.value() ) )
            {
                const auto value = colorIt.value();

                /*
                    When having a gradient vector beyond [0,1]
                    we will have gradient lines outside of the contour
                 */

                if ( value > value1 && value < value2 )
                    contourIt.setGradientLine( value, colorIt.color(), l++ );

                colorIt.advance();
            }

            const auto color = colorIt.colorAt( contourIt.value() );
            contourIt.setContourLine( color, l++ );

        } while ( contourIt.advance() );

        if ( lineCount >= 0 )
        {
            /*
                Precalculating all situations where gradient and contour lines
                are matching and doing an precise allocation makes the code
                error prone and hard to read. So we allow a defensive allocation
                strategy and simply fill up the memory with duplicates of the
                final lines.
             */
            if ( const auto count = lineCount - ( l - lines ) )
                l = QskVertex::fillUp( l, *( l - 1 ), count );
        }

        return l;
    }

    template< class ContourIterator >
    ColoredLine* fillBox( ContourIterator& contourIt,
        const QskGradient& gradient, int lineCount, ColoredLine* lines )
    {
        if ( gradient.stepCount() == 1 )
        {
            /*
                when the gradient vector does not cover the complete contour
                we need to insert gradient lines
             */
            if ( contourIt.valueBegin() >= 0.0 && contourIt.valueEnd() <= 1.0 )
            {
                SimpleColorIterator colorIt( gradient.rgbStart(), gradient.rgbEnd() );
                return fillOrdered( contourIt, colorIt, lineCount, lines );
            }
        }

        GradientColorIterator colorIt( gradient.stops() );
        return fillOrdered( contourIt, colorIt, lineCount, lines );
    }
}

#endif
