/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskBoxRenderer.h"
#include "QskRectRenderer.h"
#include "QskRoundedRectRenderer.h"
#include "QskBoxShapeMetrics.h"
#include "QskBoxBorderMetrics.h"
#include "QskBoxBorderColors.h"

#include "QskGradient.h"
#include "QskGradientDirection.h"
#include "QskFunctions.h"

static inline QskGradient qskEffectiveGradient(
    const QRectF& rect, const QskGradient gradient )
{
    if ( rect.isEmpty() )
        return QskGradient();

    const auto dir = gradient.linearDirection();

    auto g = gradient;

    if ( dir.isTilted() )
    {
        if ( g.isMonochrome() )
        {
            g.setStretchMode( QskGradient::StretchToSize );
            g.setLinearDirection( 0.0, 0.0, 0.0, 1.0 );
            g.setSpreadMode( QskGradient::PadSpread );
        }
    }
    else
    {
        /*
            Dealing with inverted gradient vectors makes the code even
            more unreadable. So we simply invert stops/vector instead.
         */
        if ( ( dir.x1() > dir.x2() ) || ( dir.y1() > dir.y2() ) )
        {
            g.setLinearDirection( dir.x2(), dir.y2(), dir.x1(), dir.y1() );

            if ( !g.isMonochrome() )
                g.setStops( qskRevertedGradientStops( gradient.stops() ) );
        }
    }

    if ( g.stretchMode() == QskGradient::StretchToSize )
        g.stretchTo( rect );

    return g;
}

static inline bool qskMaybeSpreading( const QskGradient& gradient )
{
    if ( gradient.stretchMode() == QskGradient::StretchToSize )
        return !gradient.linearDirection().contains( QRectF( 0, 0, 1, 1 ) );

    return true;
}

void QskBoxRenderer::renderBorderGeometry(
    const QRectF& rect, const QskBoxShapeMetrics& shape,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    geometry.setDrawingMode( QSGGeometry::DrawTriangleStrip );

    if ( shape.isRectangle() )
        QskRectRenderer::renderBorderGeometry( rect, border, geometry );
    else
        QskRoundedRectRenderer::renderBorderGeometry( rect, shape, border, geometry );
}

void QskBoxRenderer::renderFillGeometry(
    const QRectF& rect, const QskBoxShapeMetrics& shape, QSGGeometry& geometry )
{
    renderFillGeometry( rect, shape, QskBoxBorderMetrics(), geometry );
}

void QskBoxRenderer::renderFillGeometry(
    const QRectF& rect, const QskBoxShapeMetrics& shape,
    const QskBoxBorderMetrics& border, QSGGeometry& geometry )
{
    geometry.setDrawingMode( QSGGeometry::DrawTriangleStrip );

    if ( shape.isRectangle() )
        QskRectRenderer::renderFillGeometry( rect, border, geometry );
    else
        QskRoundedRectRenderer::renderFillGeometry( rect, shape, border, geometry );
}

void QskBoxRenderer::renderBox( const QRectF& rect,
    const QskBoxShapeMetrics& shape, const QskGradient& gradient,
    QSGGeometry& geometry )
{
    renderBox( rect, shape, QskBoxBorderMetrics(),
        QskBoxBorderColors(), gradient, geometry );
}

void QskBoxRenderer::renderBox( const QRectF& rect,
    const QskBoxShapeMetrics& shape, const QskBoxBorderMetrics& border,
    const QskBoxBorderColors& borderColors, const QskGradient& gradient,
    QSGGeometry& geometry )
{
    geometry.setDrawingMode( QSGGeometry::DrawTriangleStrip );

    const auto innerRect = qskValidOrEmptyInnerRect( rect, border.widths() );
    const auto effectiveGradient = qskEffectiveGradient( innerRect,  gradient );

    if ( shape.isRectangle() )
    {
        QskRectRenderer::renderRect(
            rect, border, borderColors, effectiveGradient, geometry );
    }
    else
    {
        QskRoundedRectRenderer::renderRect(
            rect, shape, border, borderColors, effectiveGradient, geometry );
    }
}

bool QskBoxRenderer::isGradientSupported(
    const QskBoxShapeMetrics& shape, const QskGradient& gradient )
{
    if ( !gradient.isVisible() || gradient.isMonochrome() )
        return true;

    switch( gradient.type() )
    {
        case QskGradient::Stops:
        {
            // will be rendered as vertical linear gradient
            return true;
        }
        case QskGradient::Linear:
        {
            if ( ( gradient.spreadMode() != QskGradient::PadSpread )
                && qskMaybeSpreading( gradient ) )
            {
                return false;
            }

            if ( !shape.isRectangle() )
            {
                const auto dir = gradient.linearDirection();
                if ( dir.isTilted() )
                {
                    if ( gradient.stepCount() <= 1 )
                    {
                        // interpolating between 2 colors is implemented
                        return dir.contains( QRectF( 0.0, 0.0, 1.0, 1.0 ) );
                    }
                    else
                    {
                        /*
                            when having more than 2 colors we need to insert
                            extra gradient lines and the contour also needs
                            to be rendered by lines with this specific angle.

                            This has only be implemeted for the most common
                            situation of top/left -> bottom/right
                         */
                        return ( dir.x1() == 0.0 ) && ( dir.x2() == 1.0 )
                            && ( dir.y1() == 0.0 ) && ( dir.y2() == 1.0 );
                    }
                }
            }

            return true;
        }

        default:
        {
            // Radial/Conical gradients have to be done with QskGradientMaterial
            return false;
        }
    }

    return false;
}
