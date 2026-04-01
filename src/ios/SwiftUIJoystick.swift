// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright 2021 Eden Emulator Project
// SPDX-License-Identifier: MIT

import SwiftUI

public protocol PolarCoordinate {
    /// The direction the thumb handle is pointing, up is 0° and right is 90°
    var degrees: CGFloat { get set }
    /// The thumb handle's distance from the center/origin
    var distance: CGFloat { get set }
}

public struct PolarPoint: PolarCoordinate {
    /// The direction from origin, up is 0° and right is 90°
    public var degrees: CGFloat
    /// The distance from center/origin
    public var distance: CGFloat

    public static let zero: PolarPoint = PolarPoint(degrees: 0, distance: 0)

    public init(degrees: CGFloat, distance: CGFloat) {
        self.degrees = degrees
        self.distance = distance
    }
}

/// The type of background shape used for the touch/click hitbox
///
/// Rect will allow every coordinate to be used by the joystick's thumb position
/// Circle will limit the position output to the circular area defined by the midpoint and diameter/width
public enum JoystickShape {
    /// Will allow the cursor to go from 0,0 to 0, width, and width, 0
    case rect
    /// Will limit the curser to a circular area surrounding the center of the joystick
    /// This cannot reach the corners but can reach min and max for both the x and y axis any edge's midpoint
    case circle
}

public extension CGPoint {
    internal static func +(_ lhs: CGPoint, _ rhs: CGPoint) -> CGPoint {
        return CGPoint(x: lhs.x + rhs.x, y: lhs.y + rhs.y)
    }

    internal static func -(_ lhs: CGPoint, _ rhs: CGPoint) -> CGPoint {
        return CGPoint(x: lhs.x - rhs.x, y: lhs.y - rhs.y)
    }

    internal static func *(_ lhs: CGPoint, _ rhs: CGFloat) -> CGPoint {
        return CGPoint(x: lhs.x * rhs, y: lhs.y * rhs)
    }

    func distance(to point: CGPoint) -> CGFloat {
        return sqrt(pow((point.x - x), 2) + pow((point.y - y), 2))
    }

    func getPointOnCircle(radius: CGFloat, radian: CGFloat) -> CGPoint {
        let x = self.x + radius * cos(radian)
        let y = self.y + radius * sin(radian)

        return CGPoint(x: x, y: y)
    }

    func getRadian(pointOnCircle: CGPoint) -> CGFloat {
        let originX = pointOnCircle.x - self.x
        let originY = pointOnCircle.y - self.y
        var radian = atan2(originY, originX)
        while radian < 0 {
            radian += CGFloat(2 * Double.pi)
        }
        return radian
    }

    func getPolarPoint(from origin: CGPoint = CGPoint.zero) -> PolarPoint {
        let deltaX = self.x - origin.x
        let deltaY = self.y - origin.y
        let radians = -1 * atan2(deltaY, deltaX)
        let degrees = radians * (180.0 / CGFloat.pi)
        let distance = self.distance(to: origin)

        guard degrees < 0 else {
            return PolarPoint(degrees: degrees, distance: distance)
        }
        return PolarPoint(degrees: degrees + 360.0, distance: distance)
    }
}

public class JoystickMonitor: ObservableObject {
    @Published public var xyPoint: CGPoint = .zero
    @Published public var polarPoint: PolarPoint = .zero

    public init() { }
}

/// A convenience SwiftUI ViewModifier to make a view behave like a Joystick
public struct JoystickGestureRecognizer: ViewModifier {
    @ObservedObject public var joystickMonitor: JoystickMonitor
    /// The size of the control area in which the drag gesture is monitored and reported, is diameter for a circular Joystick
    private var width: CGFloat
    /// The shape of the hitbox for the position output of the Joystick Thumb position
    private var shapeType: JoystickShape
    /// The center point of the Joystick where it goes to rest when not being used in `locksInPlace` is false
    private let midPoint: CGPoint
    /// Determines whether or not the Joystick Thumb control goes back to the center point when released
    private let locksInPlace: Bool
    @Binding private(set) public var thumbPosition: CGPoint

    /// Creates a custom joystick with the following configuration
    ///
    ///     parameter joystickMonitor: An object used to monitor the valid position of the thumb on the Joystick
    ///     parameter width: Width of the joystick control area, for a circular Joystick this is the diameter
    ///     parameter type: Shape of the hitbox for the position output of the Joystick Thumb position
    ///     parameter background: The view displayed as the Joystick background
    ///     parameter foreground: The view displayed as the Joystick Thumb Control
    ///     parameter locksInPlace: Determines if the thumb control returns to the center point when released
    public init(thumbPosition: Binding<CGPoint>, monitor: JoystickMonitor, width: CGFloat, type: JoystickShape, locksInPlace locks: Bool = false) {
        self.joystickMonitor = monitor
        self._thumbPosition = thumbPosition
        self.width = width
        self.midPoint = CGPoint(x: width / 2, y: width / 2)
        self.shapeType = type
        self.locksInPlace = locks
    }

    /// Produce the correct shape of Joystick
    public func body(content: Content) -> some View {
        switch self.shapeType {
        case .rect:
            rectBody(content)
        case .circle:
            circleBody(content)
        }
    }

    internal func getValidThumbCoordinate(for value: inout CGFloat) {
        if value <= 0 {
            value = 0
        } else if value > width {
            value = self.width
        }
    }

    internal func validateCoordinate(_ emitPoint: inout CGPoint) {
        emitPoint = emitPoint * 2
        if emitPoint.x > width {
            emitPoint.x = width
        } else if emitPoint.x < -width {
            emitPoint.x = -width
        }
        if emitPoint.y > width {
            emitPoint.y = width
        } else if emitPoint.y < -width {
            emitPoint.y = -width
        }
    }

    /// Sets the coordinates of the user's thumb to the JoystickMonitor, which emits an object change since it is an observable
    internal func emitPosition(for xyPoint: CGPoint) {
        var emitPoint = xyPoint
        validateCoordinate(&emitPoint)
        self.joystickMonitor.xyPoint = emitPoint
        self.joystickMonitor.polarPoint = emitPoint.getPolarPoint(from: self.midPoint)
    }

    /// Provides a Rectangular area in which the Joystick control can move within and report values for
    ///
    /// - parameter content: The view for which to apply the Joystick listener/DragGesture
    public func rectBody(_ content: Content) -> some View {
        content
            .contentShape(Rectangle())
            .gesture(
                DragGesture(minimumDistance: 0, coordinateSpace: .local)
                    .onChanged({ value in
                        var thumbX = value.location.x
                        var thumbY = value.location.y
                        self.getValidThumbCoordinate(for: &thumbX)
                        self.getValidThumbCoordinate(for: &thumbY)
                        self.thumbPosition = CGPoint(x: thumbX, y: thumbY)
                        let position = value.location - self.midPoint
                        self.emitPosition(for: position)
                    })
                    .onEnded({ value in
                        if !locksInPlace {
                            self.thumbPosition = self.midPoint
                            self.emitPosition(for: .zero)
                        }
                    })
                    .exclusively(
                        before:
                            LongPressGesture(minimumDuration: 0.0, maximumDistance: 0.0)
                            .onEnded({ _ in
                                if !locksInPlace {
                                    self.thumbPosition = self.midPoint
                                    self.emitPosition(for: .zero)
                                }
                            })
                    )
            )
    }

    /// Provides a Circular area in which the Joystick control can move within and report values forr
    ///
    /// - parameter content: The view for which to apply the Joystick listener/DragGesture
    public func circleBody(_ content: Content) -> some View {
        content
            .contentShape(Circle())
            .gesture(
                DragGesture(minimumDistance: 0, coordinateSpace: .local)
                    .onChanged() { value in
                        let distance = self.midPoint.distance(to: value.location)
                        if distance > self.width / 2 {
                            // Limit to radius
                           let k = (self.width / 2) / distance
                            let position = (value.location - self.midPoint) * k
                            // Order matters
                            self.thumbPosition = position + self.midPoint
                            self.emitPosition(for: position)
                        } else {
                            self.thumbPosition = value.location
                            let position = value.location - self.midPoint
                            self.emitPosition(for: position)
                        }
                    }
                    .onEnded({ value in
                        if !locksInPlace {
                            self.thumbPosition = self.midPoint
                            self.emitPosition(for: .zero)
                        }
                    })
                    .exclusively(
                        before:
                            LongPressGesture(minimumDuration: 0.0, maximumDistance: 0.0)
                            .onEnded({ _ in
                                if !locksInPlace {
                                    self.thumbPosition = self.midPoint
                                    self.emitPosition(for: .zero)
                                }
                            })
                    )
            )
    }
}

/// A convenience SwiftUI struct to make a Joystick control
public struct JoystickBuilder<background: View, foreground: View>: View {
    /// The width of the joystick control area, for a circular Joystick this is the diameter
    private(set) public var width: CGFloat
    /// The shape of the hitbox for the position output of the Joystick Thumb position
    private(set) public var controlShape: JoystickShape

    @ObservedObject private(set) public var joystickMonitor: JoystickMonitor
    @State private(set) public var thumbPosition: CGPoint = .zero
    /// The view displayed as the Joystick background, which also holds a Joystick DragGesture recognizer
    @ViewBuilder public var controlBackground: () -> background
    /// The view displayed as the Joystick Thumb Control, which also holds a Joystick DragGesture recognizer
    @ViewBuilder public var controlThumb: () -> foreground
    /// Determines whether or not the Joystick Thumb control goes back to the center point when released
    private let locksInPlace: Bool

    /// Creates a custom joystick with two views that are passed to it
    ///
    ///     parameter position: Will output the valid position of the thumb on the Joystick, from 0 to width
    ///     parameter width: Width of the joystick control area, for a circular Joystick this is the diameter
    ///     parameter shape: Shape of the hitbox for the position output of the Joystick Thumb position
    ///     parameter background: The view displayed as the Joystick background
    ///     parameter foreground: The view displayed as the Joystick Thumb Control
    ///     parameter locksInPlace: Determines if the thumb control returns to the center point when released
    public init(monitor: JoystickMonitor, width: CGFloat, shape: JoystickShape, @ViewBuilder background: @escaping () -> background, @ViewBuilder foreground: @escaping () -> foreground, locksInPlace locks: Bool) {
        self.joystickMonitor = monitor
        self.width = width
        self.controlShape = shape
        self.controlBackground = background
        self.controlThumb = foreground
        self.locksInPlace = locks
    }

    public var body: some View {
        controlBackground()
            .frame(width: self.width, height: self.width)
            .joystickGestureRecognizer(thumbPosition: self.$thumbPosition, monitor: self.joystickMonitor, width: self.width, shape: self.controlShape, locksInPlace: self.locksInPlace)
            .overlay(
                controlThumb()
                    .frame(width: self.width / 4, height: self.width / 4)
                    .position(x: self.thumbPosition.x, y: self.thumbPosition.y)
                    .joystickGestureRecognizer(thumbPosition: self.$thumbPosition, monitor: self.joystickMonitor, width: self.width, shape: self.controlShape, locksInPlace: self.locksInPlace)
            )
            .onAppear(perform: {
                let midPoint = self.width / 2
                self.thumbPosition = CGPoint(x: midPoint, y: midPoint)
            })
    }
}

public extension View {
    /// Convenience modifier for adding a Joystick recognizer
    /// Creates a custom joystick with the following configuration
    ///
    ///     parameter joystickMonitor: An object used to monitor the valid position of the thumb on the Joystick
    ///     parameter width: Width of the joystick control area, for a circular Joystick this is the diameter
    ///     parameter shape: (.rect || .circle) - Shape of the hitbox for the position output of the Joystick Thumb position
    ///     parameter background: The view displayed as the Joystick background
    ///     parameter foreground: The view displayed as the Joystick Thumb Control
    ///     parameter locksInPlace: default false - Determines if the thumb control returns to the center point when released
    ///     parameter locksInPlace: default false - Determines if the thumb control returns to the center point when released
    func joystickGestureRecognizer(thumbPosition: Binding<CGPoint>, monitor: JoystickMonitor, width: CGFloat, shape: JoystickShape, locksInPlace locks: Bool = false) -> some View {
        modifier(JoystickGestureRecognizer(thumbPosition: thumbPosition, monitor: monitor, width: width, type: shape, locksInPlace: locks))
    }
}
