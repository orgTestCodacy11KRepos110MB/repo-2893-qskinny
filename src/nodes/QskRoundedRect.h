/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#ifndef QSK_ROUNDED_RECT_H
#define QSK_ROUNDED_RECT_H

#include "QskVertex.h"
#include "QskRoundedRectRenderer.h"

class QskBoxShapeMetrics;
class QskBoxBorderMetrics;
class QskBoxBorderColors;

namespace QskRoundedRect
{
    enum
    {
        TopLeft = Qt::TopLeftCorner,
        TopRight = Qt::TopRightCorner,
        BottomLeft = Qt::BottomLeftCorner,
        BottomRight = Qt::BottomRightCorner
    };

    class Metrics
    {
      public:
        Metrics( const QRectF&, const QskBoxShapeMetrics&, const QskBoxBorderMetrics& );

        QskVertex::Quad outerQuad;
        QskVertex::Quad innerQuad;
        QskVertex::Quad centerQuad;

        struct Corner
        {
            inline qreal xInner( qreal cos ) const
            {
                return centerX + sx * ( x0 + cos * rx );
            }

            inline qreal yInner( qreal sin ) const
            {
                return centerY + sy * ( y0 + sin * ry );
            }

            inline qreal xOuter( qreal cos ) const
            {
                return centerX + sx * ( cos * radiusX );
            }

            inline qreal yOuter( qreal sin ) const
            {
                return centerY + sy * ( sin * radiusY );
            }

            inline void setBorderLine( qreal cos, qreal sin,
                QskVertex::Color color, QskVertex::ColoredLine* line ) const
            {
                line->setLine( xInner( cos ), yInner( sin ),
                    xOuter( cos ), yOuter( sin ), color );
            }

            inline void setBorderLine( qreal cos, qreal sin, QskVertex::Line* line ) const
            {
                line->setLine( xInner( cos ), yInner( sin ),
                    xOuter( cos ), yOuter( sin ) );
            }

            bool isCropped;

            qreal centerX, centerY;
            qreal radiusX, radiusY;
            qreal radiusInnerX, radiusInnerY;

            qreal x0, rx;
            qreal y0, ry;

            qreal sx, sy;

            int stepCount;

        } corner[ 4 ];

        bool isBorderRegular;
        bool isRadiusRegular;
        bool isTotallyCropped;

        Qt::Orientations stepSizeSymmetries;
        Qt::Orientation preferredOrientation;
    };

    class Stroker
    {
      public:
        inline Stroker( const Metrics& metrics )
            : m_metrics( metrics )
        {
        }

        int fillLineCount() const;

        int fillLineCount( const QskGradient& ) const;
        int borderLineCount( const QskBoxBorderColors& ) const;

        void createBorderLines( QskVertex::Line* ) const;
        void createFillLines( QskVertex::Line* ) const;

        void createBox(
            QskVertex::ColoredLine*, const QskBoxBorderColors&,
            QskVertex::ColoredLine*, const QskGradient& ) const;

        void createFill( QskVertex::ColoredLine*, const QskGradient& ) const;
        void createBorder( QskVertex::ColoredLine*, const QskBoxBorderColors& ) const;

        void createFillFanLines( QSGGeometry& );

      private:
        void createRegularBorderLines( QskVertex::Line* ) const;
        void createIrregularBorderLines( QskVertex::Line* ) const;

        void createRegularBorder(
            QskVertex::ColoredLine*, const QskBoxBorderColors& ) const;

        void createIrregularBorder(
            QskVertex::ColoredLine*, const QskBoxBorderColors& ) const;

        void setBorderGradientLines( Qt::Edge,
            const QskBoxBorderColors&, QskVertex::ColoredLine* ) const;

        void createRegularBox(
            QskVertex::ColoredLine*, const QskBoxBorderColors&,
            QskVertex::ColoredLine*, const QskGradient& ) const;

        const Metrics& m_metrics;
    };
}

#endif
