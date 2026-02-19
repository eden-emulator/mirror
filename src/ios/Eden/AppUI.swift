//
//  AppUI.swift - Sudachi
//  Created by Jarrod Norwell on 4/3/2024.
//

import Foundation
import QuartzCore.CAMetalLayer

public struct AppUI {

    public static let shared = AppUI()

    fileprivate let appUIObjC = AppUIObjC.shared()

    public func configure(layer: CAMetalLayer, with size: CGSize) {
        appUIObjC.configure(layer: layer, with: size)
    }

    public func information(for url: URL) -> AppUIInformation {
        appUIObjC.gameInformation.information(for: url)
    }

    public func insert(game url: URL) {
        appUIObjC.insert(game: url)
    }

    public func insert(games urls: [URL]) {
        appUIObjC.insert(games: urls)
    }

    public func bootOS() {
        appUIObjC.bootOS()
    }

    public func pause() {
        appUIObjC.pause()
    }

    public func play() {
        appUIObjC.play()
    }

    public func ispaused() -> Bool {
        return appUIObjC.ispaused()
    }

    public func FirstFrameShowed() -> Bool {
        return appUIObjC.hasfirstfame()
    }

    public func canGetFullPath() -> Bool {
        return appUIObjC.canGetFullPath()
    }


    public func exit() {
        appUIObjC.quit()
    }

    public func step() {
        appUIObjC.step()
    }

    public func orientationChanged(orientation: UIInterfaceOrientation, with layer: CAMetalLayer, size: CGSize) {
        appUIObjC.orientationChanged(orientation: orientation, with: layer, size: size)
    }

    public func touchBegan(at point: CGPoint, for index: UInt) {
        appUIObjC.touchBegan(at: point, for: index)
    }

    public func touchEnded(for index: UInt) {
        appUIObjC.touchEnded(for: index)
    }

    public func touchMoved(at point: CGPoint, for index: UInt) {
        appUIObjC.touchMoved(at: point, for: index)
    }

    public func gyroMoved(x: Float, y: Float, z: Float, accelX: Float, accelY: Float, accelZ: Float, controllerId: Int32, deltaTimestamp: Int32) {
        // Calling the Objective-C function with both gyroscope and accelerometer data
        appUIObjC.virtualControllerGyro(controllerId,
            deltaTimestamp: deltaTimestamp,
            gyroX: x, gyroY: y, gyroZ: z,
            accelX: accelX, accelY: accelY, accelZ: accelZ)
    }


    public func thumbstickMoved(analog: VirtualControllerAnalogType, x: Float, y: Float, controllerid: Int) {
        appUIObjC.thumbstickMoved(analog, x: CGFloat(x), y: CGFloat(y), controllerId: Int32(controllerid))
    }

    public func virtualControllerButtonDown(button: VirtualControllerButtonType, controllerid: Int) {
        appUIObjC.virtualControllerButtonDown(button, controllerId: Int32(controllerid))
    }

    public func virtualControllerButtonUp(button: VirtualControllerButtonType, controllerid: Int) {
        appUIObjC.virtualControllerButtonUp(button, controllerId: Int32(controllerid))
    }

    public func settingsSaved() {
        appUIObjC.settingsChanged()
    }
}
