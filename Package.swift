// swift-tools-version: 5.9
//
// SPDX-FileCopyrightText: 2026 Stephen F. Booth <contact@sbooth.dev>
// SPDX-License-Identifier: MIT
//
// Part of https://github.com/sbooth/CXXQueue
//

import PackageDescription

let package = Package(
    name: "CXXQueue",
    products: [
        .library(
            name: "CXXQueue",
            targets: [
                "CXXQueue",
            ]
        ),
    ],
    targets: [
        .target(
            name: "CXXQueue"
        ),
        .testTarget(
            name: "CXXQueueTests",
            dependencies: [
                "CXXQueue",
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx20
)
