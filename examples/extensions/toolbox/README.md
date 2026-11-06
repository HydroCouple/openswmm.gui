# Toolbox example

Build this independent plugin against an installed GUI SDK 1.10.

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/gui-sdk
cmake --build build
```

Add the built module directory through **Tools → Plugins…**, then restart the
GUI. Open **View → Panels → Analysis Toolbox** and expand **Toolbox example**.
The **Square a value…** and **Double a value…** commands each open their own
dockable panel. Their ribbon commands open those same panels.

Check the **Calculation** tag to see both tools, or also check **Square** to
narrow the tree. Enter a value and choose **Calculate**. The result and its
message appear in the tool panel and in the toolbox's **History** tab.

The example uses only public SDK and Qt headers. Factories create widgets on
first activation. Tool registration binds the command automatically. Each
calculation creates a fresh execution ID and explicitly finishes it after
publishing a literal result.
